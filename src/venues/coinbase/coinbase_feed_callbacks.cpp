#include <slick/logger.hpp>
#include "coinbase_exchange.hpp"
#include "coinbase_live_ws_feed.hpp"
#include <common/symbol_manager.hpp>
#include <utils/timestamp.hpp>
#include <charconv>

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

    if (symbols.empty())
    {
        return;
    }

    // Mark symbols as pending subscription so the snapshot is re-requested on reconnect
    for (const auto &sym : symbols)
    {
        auto *symbol = sym_mgr.getSymbol(sym, venue_);
        if (symbol)
        {
            pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2].emplace(symbol);
            pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES].emplace(symbol);
            symbol->clearTradeHistory();
        }
    }

    // Restart the feed that dropped rather than building a replacement. A
    // WebSocketClient registers its producers on the shared multiplexer from its
    // constructor, and stream_buffer_multiplexer has no way to unregister them - so a
    // second client throws std::invalid_argument on the first duplicate id, uncaught
    // on the exchange thread. Handing it a fresh offset instead would only trade that
    // for a leak: the multiplexer owns each producer's stream_buffer, so every
    // reconnect would strand the 64MB market-data and 16MB user-data rings of the
    // client it replaced. Neither is necessary - `start()` calls
    // WebSocketClient::subscribe(), which reopens a socket whose status is past
    // CONNECTED and then re-sends the subscription.
    disconnected_feed->start();

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
    auto *symbol = sym_mgr.getSymbol(snapshot.product_id, venue_);
    if (!symbol) [[unlikely]]
    {
        return;
    }

    auto &pending_l2_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2];
    auto it = pending_l2_subscriptions.find(symbol);
    // clearMDOrders(), not clear() - the contract this rebuild has to honour, and
    // what Hyperliquid's snapshot path already calls. OrderBookL3::clear() unlinks
    // orders without notifying observers, so feed_md_level_quantity_ kept every
    // pre-snapshot price and the rebuild below added the snapshot's quantities on
    // top of stale ones, leaving reconcilePhantomQty measuring against a level
    // quantity the book never held. It also wiped the simulator's own resting orders
    // out of the L3 book while orders_ went on holding them - so after a reconnect a
    // client's live order was gone from the book, unable to fill or be swept, but
    // still reported as resting.
    symbol->order_book_->clearMDOrders();

    // Everything still queued describes the book that clear() just discarded, so it
    // must not be replayed onto the rebuilt one. poll() drains the callbacks that
    // delivered this snapshot and only then runs processSequencedEvents, so a trade
    // received before a reconnect would otherwise match against fresh liquidity and
    // fabricate fills - dispatchEvent's TRADE branch has no stale-timestamp guard to
    // stop it, and the level branch's guard only compares against the rebuilt book.
    // Everything queued at this point does predate the snapshot: every callback runs
    // on the exchange thread in arrival order, so whatever arrives after this one is
    // enqueued after this line.
    auto &state = symbol_event_state_[symbol];
    state.pending_events = {};

    // The level-update batch in flight describes the book that was just discarded.
    // It is only flushed downstream when a later event carries a different sequence,
    // so leaving it here published pre-reset levels *after* the snapshot frame, as an
    // ordinary incremental update - rebuilding liquidity the reset had removed. The
    // publish boundary is re-primed too, so the first event after the snapshot starts
    // a batch the way it would on a fresh subscription.
    state.level_update_buffer_.clear();
    state.last_published_level_seq_num = 0;

    // One stamp for the whole snapshot: every update in it shares a sequence, and
    // equal sequences pass the book's guard. It comes from the same counter the
    // dispatch loop uses - this path bypasses the event queue entirely, and the two
    // must not stamp the book out of different sequence spaces.
    const uint64_t book_seq_num = nextBookSeqNum(symbol, state);
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
        auto trade_summaries = symbol->matching_engine_->match(side, order_id, price, qty, *symbol->order_book_.get(), update.event_time, book_seq_num);
        if (qty)
        {
            symbol->order_book_->addBookOrder(order_id, book_side, price, qty, update.event_time, book_seq_num, it_update == it_last);
        }

        if (!trade_summaries.empty())
        {
            for (const auto &summary : trade_summaries)
            {
                publishTradeSummary(symbol, summary);
            }
        }

        // No incremental row is buffered for a snapshot level. populateL2Snapshot()
        // below already publishes the rebuilt book in full, so these only duplicated
        // it - arriving later, whenever the next event happened to change sequence,
        // and carrying a level_index read while the rebuild was still in progress and
        // therefore wrong by the time the level settled.

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
    auto *symbol = sym_mgr.getSymbol(update_batch.product_id, venue_);
    if (!symbol) [[unlikely]]
    {
        return;
    }

    // Get or create event state for this symbol
    auto &state = symbol_event_state_[symbol];

    // Add each update to per-symbol pending_events queue.
    //
    // Both enqueue loops in this file walk their batch in reverse *dispatch* order:
    // the element visited first takes the lowest sequence_id, and EventCompare pops
    // the highest first, so the element enqueued first is dispatched last. The
    // end-of-event marker therefore belongs on the element the loop visits first.
    // `std::prev(rend())` is the opposite one - enqueued last, dispatched *first* -
    // so marking it closed every batch on its opening update, and a client batching
    // until the marker acted on one update out of the whole batch.
    //
    // onLevel2Snapshot is not the same shape: it applies its batch inline with no
    // queue to reverse it, so the equivalent test there really is the final element.
    const auto it_dispatched_last = update_batch.updates.rbegin();
    for (auto it = update_batch.updates.rbegin(); it != update_batch.updates.rend(); ++it)
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
        evt.flags = it == it_dispatched_last ? UpdateFlags::F_END_EVENT : UpdateFlags::F_NONE;

        // Update last_event_time to track latest event_time seen
        if (state.last_event_time < evt.event_time)
        {
            state.last_event_time = evt.event_time;
        }

        // LOG_DEBUG("Received L2 update for symbol {}: price={}, qty={}, side={}, event_time={}, seq_num={}, flags={}",
        //     symbol->symbol_.view(), evt.price, evt.qty, evt.side, evt.event_time, evt.seq_num, evt.flags);

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
    Symbol *symbol = sym_mgr.getSymbol(snapshots[0].product_id, venue_);
    if (!symbol || !pending_subscriptions.contains(symbol))
    {
        return;
    }

    // These prints happened before we were listening, so they only seed the
    // history that serves the subscribe snapshot - they are never fed to the
    // matching engine. Coinbase sends them newest first, so walk in reverse to
    // store them chronologically.
    for (auto i = static_cast<int32_t>(snapshots.size()) - 1; i >= 0; i--)
    {
        const auto &trade = snapshots[i];

        // Keep the venue's own trade id, and keep the ids this exchange mints
        // afterwards continuing the venue's sequence rather than colliding with it.
        uint64_t trade_id = 0;
        auto [_, ec] = std::from_chars(trade.trade_id.data(), trade.trade_id.data() + trade.trade_id.size(), trade_id);
        if (ec != std::errc{})
        {
            trade_id = nextVenueTradeId();
        }
        else if (trade_id >= next_trade_id_)
        {
            next_trade_id_ = trade_id + 1;
        }

        symbol->recordTrade(MDTrade{
            .trade_id = trade_id,
            .event_time = trade.time,
            .seq_num = seq_num,
            .price = to_price_t(trade.price),
            .qty = to_qty_t(trade.size),
            // Never matched into the book, so whether a subscriber's book
            // snapshot reflects this print depends on its timestamp.
            .flags = UpdateFlags::F_NOT_IN_BOOK,
            .side = static_cast<Side>(trade.side)});
    }

    publishTradeSubscriptionResponse(symbol, coinbase::WebSocketChannel::MARKET_TRADES);
    pending_subscriptions.erase(symbol);
}

void CoinbaseExchange::onMarketTrades(WebSocketClient * /* client */, uint64_t seq_num, const std::vector<coinbase::MarketTrade> &trades)
{
    // Coinbase sends market_trades newest-first, so this loop runs forward and the
    // batch is dispatched back-to-front - see onLevel2Updates. trades.begin() is
    // enqueued first and therefore dispatched last, so it carries the marker.
    for (auto it = trades.begin(); it != trades.end(); ++it)
    {
        const auto &trade = *it;
        auto *symbol = sym_mgr.getSymbol(trade.product_id, venue_);
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
        evt.flags = it == trades.begin() ? UpdateFlags::F_END_EVENT : UpdateFlags::F_NONE;

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