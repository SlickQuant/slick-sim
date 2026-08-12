#include <slick/logger.hpp>
#include "exch_coinbase.hpp"
#include <md_feed/coinbase_live_ws_feed.hpp>
#include <common/symbol_manager.hpp>
#include <utils/timestamp.hpp>

using namespace slick::sim::exch;
using namespace slick::sim;

namespace
{
    auto &sym_mgr = SymbolManager::instance();
}

void CoinbaseExchange::onMarketDataConnected(WebSocketClient *client)
{
    LOG_INFO("WebSocket {:p} market data connected", (void *)client);
}

void CoinbaseExchange::onMarketDataDisconnected(WebSocketClient *client)
{
    LOG_INFO("WebSocket {:p} market data disconnected", (void *)client);

    // Find the feed that owns this client
    std::shared_ptr<md_feed::MDFeed> disconnected_feed;
    for (auto &feed : md_feeds_)
    {
        auto *live_feed = dynamic_cast<md_feed::CoinbaseLiveWSFeed *>(feed.get());
        if (live_feed && live_feed->getClient() == client)
        {
            disconnected_feed = feed;
            break;
        }
    }

    if (!disconnected_feed)
    {
        LOG_WARN("WebSocket {:p} disconnected but no matching feed found", (void *)client);
        return;
    }

    // Collect symbols associated with this feed
    std::vector<std::string> symbols;
    for (auto &[sym, feed] : map_symbol_feed_)
    {
        if (feed == disconnected_feed)
        {
            symbols.push_back(sym);
        }
    }

    // Remove old feed from tracking
    md_feeds_.erase(std::remove(md_feeds_.begin(), md_feeds_.end(), disconnected_feed), md_feeds_.end());
    for (const auto &sym : symbols)
    {
        map_symbol_feed_.erase(sym);
    }

    if (symbols.empty())
    {
        return;
    }

    // Mark symbols as pending subscription so the snapshot is re-requested on reconnect
    for (const auto &sym : symbols)
    {
        auto *symbol = sym_mgr.getSymbol(sym);
        if (symbol)
        {
            pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2].emplace(symbol);
            pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES].emplace(symbol);
            trade_snapshots_.erase(symbol);
        }
    }

    // Create and start a new feed
    auto new_feed = std::make_shared<md_feed::CoinbaseLiveWSFeed>(ws_mux_, this, symbols);
    md_feeds_.emplace_back(new_feed);
    for (const auto &sym : symbols)
    {
        map_symbol_feed_.emplace(sym, new_feed);
    }
    new_feed->start();

    LOG_INFO("WebSocket {:p} restarted market data feed for {} symbol(s)", (void *)client, symbols.size());
}

void CoinbaseExchange::onUserDataConnected(WebSocketClient *client)
{
    LOG_INFO("WebSocket {:p} user data connected", (void *)client);
}

void CoinbaseExchange::onUserDataDisconnected(WebSocketClient *client)
{
    LOG_INFO("WebSocket {:p} user data disonnected", (void *)client);
}

void CoinbaseExchange::onLevel2Snapshot(WebSocketClient * /* client */, uint64_t seq_num, const coinbase::Level2UpdateBatch &snapshot)
{
    auto *symbol = sym_mgr.getSymbol(snapshot.product_id);
    if (!symbol) [[unlikely]]
    {
        return;
    }

    auto &pending_l2_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2];
    auto it = pending_l2_subscriptions.find(symbol);
    symbol->order_book_->clear();
    std::vector<MDLevel> level_updates;
    std::vector<MDTrade> trade_updates;

    for (auto it_update = snapshot.updates.rbegin(), it_last = std::prev(snapshot.updates.rend()); it_update != snapshot.updates.rend(); ++it_update)
    {
        auto &update = *it_update;
        auto price = to_price_t(update.price_level);
        auto side = static_cast<Side>(update.side);
        auto book_side = to_book_side(side);
        auto qty = to_qty_t(update.new_quantity);

        auto order_id = utils::nextOrderId();
        auto trade_summaries = symbol->matching_engine_->match(side, order_id, price, qty, *symbol->order_book_.get(), update.event_time, seq_num);
        if (qty)
        {
            symbol->order_book_->addOrder(order_id, book_side, price, qty, update.event_time, seq_num, it_update == it_last);
        }

        if (!trade_summaries.empty())
        {
            for (const auto &summary : trade_summaries)
            {
                publishTradeSummary(symbol->symbol_.c_str(), summary);
            }
        }

        if (it == pending_l2_subscriptions.end())
        {
            level_update_buffer_.emplace_back(MDLevel{
                .event_time = update.event_time,
                .seq_num = seq_num,
                .price = price,
                .qty = qty,
                .num_orders = 1,
                .flags = static_cast<UpdateFlags>(UpdateFlags::F_IS_SNAPSHOT | (it_update == it_last ? UpdateFlags::F_END_EVENT : UpdateFlags::F_NONE)),
                .side = side});
        }

        if (update.event_time > symbol->order_book_->lastUpdateTime())
        {
            symbol->order_book_->setLastUpdate(update.event_time, seq_num);
        }
    }

    if (it != pending_l2_subscriptions.end())
    {
        symbol->order_book_->populateL2SubscriptionResponse(md_queue_, coinbase::WebSocketChannel::LEVEL2);
        pending_l2_subscriptions.erase(it);
        symbol->md_level_update_cache_.clear();
        // TODO: publish MD order update
        symbol->md_order_update_cache_.clear();
    }
    else
    {
        symbol->order_book_->populateL2Snapshot(md_queue_);
        symbol->md_level_update_cache_.clear();
        // TODO: publish MD order update
        symbol->md_order_update_cache_.clear();
    }

    // publishMDBookUpdate(symbol->id_, *symbol->order_book_.get(), indices);
    LOG_DEBUG(symbol->order_book_->to_string());
}

void CoinbaseExchange::onLevel2Updates(WebSocketClient * /* client */, uint64_t seq_num, const coinbase::Level2UpdateBatch &update_batch)
{
    auto *symbol = sym_mgr.getSymbol(update_batch.product_id);
    if (!symbol) [[unlikely]]
    {
        return;
    }

    // Get or create event state for this symbol
    auto &state = symbol_event_state_[symbol];

    // Add each update to per-symbol pending_events queue
    for (auto it = update_batch.updates.rbegin(), it_last = std::prev(update_batch.updates.rend()); it != update_batch.updates.rend(); ++it)
    {
        auto &update = *it;
        Event evt;
        evt.type = EventType::LEVEL_UPDATE;
        evt.event_time = update.event_time;         // Use Coinbase timestamp
        evt.sequence_id = state.next_sequence_id++; // Internal counter
        evt.seq_num = seq_num;
        evt.received_time = utils::get_current_time_ns();
        evt.price = to_price_t(update.price_level);
        evt.qty = to_qty_t(update.new_quantity);
        evt.side = static_cast<Side>(update.side);
        evt.flags = it == it_last ? UpdateFlags::F_END_EVENT : UpdateFlags::F_NONE;

        // Update last_event_time to track latest event_time seen
        if (state.last_event_time < evt.event_time)
        {
            state.last_event_time = evt.event_time;
        }

        // LOG_DEBUG("Received L2 update for symbol {}: price={}, qty={}, side={}, event_time={}, seq_num={}, flags={}",
        //     symbol->symbol_, evt.price, evt.qty, evt.side, evt.event_time, evt.seq_num, evt.flags);

        // Add to per-symbol priority queue
        state.pending_events.push(std::move(evt));
    }
}

void CoinbaseExchange::onMarketTradesSnapshot(WebSocketClient * /* client */, uint64_t seq_num, const std::vector<coinbase::MarketTrade> &snapshots)
{
    auto &pending_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES];
    if (pending_subscriptions.empty() || snapshots.empty())
    {
        return;
    }
    Symbol *symbol = sym_mgr.getSymbol(snapshots[0].product_id);
    if (!symbol || !pending_subscriptions.contains(symbol))
    {
        return;
    }

    auto it = trade_snapshots_.find(symbol);
    if (it == trade_snapshots_.end())
    {
        it = trade_snapshots_.emplace(symbol, utils::RingBuffer<Event>(16)).first;
    }

    auto &trade_snapshot = it->second;

    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(MDTradeUpdate) + snapshots.size() * sizeof(MDTrade));
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    memcpy(update->symbol, symbol->symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse *>(update->data);
    response->channel = coinbase::WebSocketChannel::MARKET_TRADES;

    auto *snapshot = reinterpret_cast<MDTradeUpdate *>(response->data);
    snapshot->num_trades = static_cast<uint32_t>(snapshots.size());

    auto *trades = reinterpret_cast<MDTrade *>(snapshot->trades);

    auto &state = symbol_event_state_[symbol];

    // Coinbase trades are in reverse chronological order
    for (auto i = static_cast<int32_t>(snapshots.size()) - 1; i >= 0; i--)
    {
        const auto &trade = snapshots[i];
        trades->price = to_price_t(trade.price);
        trades->qty = to_qty_t(trade.size);
        trades->side = static_cast<Side>(trade.side);

        // Store in trade history ring buffer
        Event evt;
        evt.type = EventType::TRADE;
        evt.event_time = trade.time;
        evt.seq_num = seq_num;
        evt.sequence_id = state.next_sequence_id++;
        evt.received_time = utils::get_current_time_ns();
        evt.price = to_price_t(trade.price);
        evt.qty = to_qty_t(trade.size);
        evt.side = static_cast<Side>(trade.side);
        trade_snapshot.emplace(std::move(evt));

        trades++;
    }
    for (const auto &trade : snapshots)
    {
        trades->price = to_price_t(trade.price);
        trades->qty = to_qty_t(trade.size);
        trades->side = static_cast<Side>(trade.side);
        trades++;
    }
    md_queue_.publish(index, sz);
    pending_subscriptions.erase(symbol);
}

void CoinbaseExchange::onMarketTrades(WebSocketClient * /* client */, uint64_t seq_num, const std::vector<coinbase::MarketTrade> &trades)
{
    for (const auto &trade : trades)
    {
        auto *symbol = sym_mgr.getSymbol(trade.product_id);
        if (!symbol) [[unlikely]]
        {
            continue;
        }

        // Get or create event state for this symbol
        auto &state = symbol_event_state_[symbol];

        Event evt;
        evt.type = EventType::TRADE;
        evt.event_time = trade.time;
        evt.seq_num = seq_num;
        evt.sequence_id = state.next_sequence_id++; // Internal counter
        evt.received_time = utils::get_current_time_ns();
        evt.price = to_price_t(trade.price);
        evt.qty = to_qty_t(trade.size);
        evt.side = static_cast<Side>(trade.side);

        // Update last_event_time to track latest event_time seen
        if (state.last_event_time < evt.event_time)
        {
            state.last_event_time = evt.event_time;
        }

        // Add to per-symbol priority queue
        state.pending_events.push(std::move(evt));
    }
}

void CoinbaseExchange::onMarketDataGap(WebSocketClient * /* client */)
{
    LOG_WARN("CoinbaseExchange Market Data Gap detected");
}
void CoinbaseExchange::onUserDataGap(WebSocketClient * /* client */)
{
    LOG_WARN("CoinbaseExchange User Data Gap detected");
}
void CoinbaseExchange::onMarketDataError(WebSocketClient *client, std::string &&err)
{
    LOG_ERROR("Websocket {:p} onMarketDataError {}", (void *)client, std::move(err));
}

void CoinbaseExchange::onUserDataError(WebSocketClient *client, std::string &&err)
{
    LOG_ERROR("Websocket {:p} onUserDataError {}", (void *)client, std::move(err));
}