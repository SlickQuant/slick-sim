#include "exch_coinbase.hpp"
#include <md_feed/coinbase_live_ws_feed.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <common/symbol_manager.hpp>
#include <coinbase/websocket.hpp>
#include <utils/timestamp.hpp>
#include <utils/price.hpp>
#include <chrono>
#include <order_gateway/coinbase_rest_order_gateway.hpp>
#include <order_gateway/coinbase_ws_order_gateway.hpp>
#include <market_data_publisher/coinbase_publisher.hpp>

using namespace slick::sim;
using namespace slick::sim::exch;
using namespace slick::sim::order_gateway;
using namespace slick::sim::md_publisher;

namespace
{
    auto &sym_mgr = SymbolManager::instance();
}

CoinbaseExchange::CoinbaseExchange(const nlohmann::json &config)
    : Exchange(Venue::COINBASE, config), coinbase::UserThreadWebsocketCallbacks(), ws_mux_(config.value("md_queue_size", 16777216))
{
    md_publisher_ = std::make_unique<CoinbasePublisher>(config["md_publisher"], request_queue_, md_queue_);

    auto &og_config = config["order_gateway"];
    if (!og_config.contains("rest"))
    {
        throw std::runtime_error("COINBASE Exchange order_gateway missing rest config");
    }
    order_gateways_.emplace_back(
        std::make_unique<CoinbaseRestOrderGateway>(og_config["rest"], request_queue_, response_queue_));
    if (og_config.contains("ws"))
    {
        order_gateways_.emplace_back(
            std::make_unique<CoinbaseWebsocketOrderGateway>(og_config["ws"], request_queue_, response_queue_));
    }

    if (config.contains("md_feeds"))
    {
        for (const auto &md_feed_config : config["md_feeds"])
        {
            std::string type = md_feed_config.value("type", "");
            if (type == "coinbase_live_ws")
            {
                use_live_feed_ = true;
            }
        }
    }
}

void CoinbaseExchange::start()
{
    if (!enabled_)
    {
        LOG_WARN("Exchange {} is disabled, skipping start", to_string(venue_));
        return;
    }

    run_.store(true, std::memory_order_release);

    for (auto &og : order_gateways_)
    {
        og->start();
    }
    md_publisher_->start();

    if (md_feed_)
    {
        md_feed_->start();
    }

    thread_ = std::thread([this]()
                          {
        while (run_.load(std::memory_order_relaxed)) {
            if (md_feed_ || !md_feeds_.empty()) {
                processData();
            }
            processSequencedEvents();
            processRequest();
        } });
    // if (md_feed_) {
    //     md_feed_->stop();
    // }
}

Symbol *CoinbaseExchange::addSymbol(std::string_view product_id)
{
    assert(!sym_mgr.getSymbol(product_id));
    auto symbol = sym_mgr.createSymbol(product_id, Venue::COINBASE);
    assert(symbol);
    if (!matching_engines_[engine::MatchingEngine::Type::FIFO])
    {
        matching_engines_[engine::MatchingEngine::Type::FIFO] = std::make_unique<engine::FifoMatchingEngine>(response_queue_);
    }
    symbol->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
    symbol->createOrderBook<OrderBookType::L2>();
    return symbol;
}

void CoinbaseExchange::handleMdSubscription(const Request &request)
{
    const auto &msg = request.md_subscription;
    std::string sym = request.symbol;
    auto *symbol = sym_mgr.getSymbol(sym);
    if (!symbol)
    {
        symbol = addSymbol(sym);
        if (use_live_feed_)
        {
            switch (static_cast<coinbase::WebSocketChannel>(msg.channel))
            {
            case coinbase::WebSocketChannel::LEVEL2:
            {
                pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2].emplace(symbol);
                break;
            }
            case coinbase::WebSocketChannel::MARKET_TRADES:
            {
                pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES].emplace(symbol);
                break;
            }
            case coinbase::WebSocketChannel::HEARTBEATS:
                break;
            default:
                LOG_WARN("Unsupported channel {} for symbol {}", static_cast<int>(msg.channel), sym);
                return;
            }
            md_feeds_.emplace_back(std::make_shared<md_feed::CoinbaseLiveWSFeed>(ws_mux_, this, std::vector<std::string>{sym}));
            auto &feed = md_feeds_.back();
            feed->start();
            map_symbol_feed_.emplace(sym, feed);
        }
        else
        {
            // TODO: reject?
            switch (static_cast<coinbase::WebSocketChannel>(msg.channel))
            {
            case coinbase::WebSocketChannel::LEVEL2:
            {
                symbol->order_book_->populateL2SubscriptionResponse(md_queue_, msg.channel);
                break;
            }
            case coinbase::WebSocketChannel::MARKET_TRADES:
            {
                populateMDTradesResponse(symbol);
                break;
            }
            case coinbase::WebSocketChannel::HEARTBEATS:
                break;
            default:
                LOG_WARN("Unsupported channel {} for symbol {}", static_cast<int>(msg.channel), sym);
                return;
            }
        }
        symbol->num_subscriptions_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    symbol->num_subscriptions_.fetch_add(1, std::memory_order_relaxed);
    switch (static_cast<coinbase::WebSocketChannel>(msg.channel))
    {
    case coinbase::WebSocketChannel::LEVEL2:
    {
        auto &pending_l2_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2];
        if (pending_l2_subscriptions.find(symbol) == pending_l2_subscriptions.end())
        {
            // subscription already received, publish snapshot immediately
            symbol->order_book_->populateL2SubscriptionResponse(md_queue_, msg.channel);
        }
        else
        {
            // Already pending, do nothing
        }
        break;
    }
    case coinbase::WebSocketChannel::MARKET_TRADES:
    {
        auto &pending_trade_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES];
        if (pending_trade_subscriptions.find(symbol) == pending_trade_subscriptions.end())
        {
            // subscription already received, publish snapshot immediately
            populateMDTradesResponse(symbol);
        }
        else
        {
            // Already pending, do nothing
        }
        break;
    }
    case coinbase::WebSocketChannel::TICKER:
        break;
    }
}

void CoinbaseExchange::handleMdUnsubscription(const Request &request)
{
    std::string sym = request.symbol;
    auto *symbol = sym_mgr.getSymbol(sym);
    if (!symbol)
    {
        return;
    }
    if (symbol->num_subscriptions_.fetch_sub(1, std::memory_order_relaxed) == 1)
    {
        auto it_feed = map_symbol_feed_.find(sym);
        if (it_feed != map_symbol_feed_.end())
        {
            auto feed = it_feed->second;
            feed->stop();
            md_feeds_.erase(std::remove(md_feeds_.begin(), md_feeds_.end(), feed), md_feeds_.end());
            map_symbol_feed_.erase(it_feed);
        }
    }
}

void CoinbaseExchange::processSequencedEvents(bool force)
{
    const uint64_t now = utils::get_current_time_ns();

    // Process events from all symbols
    for (auto &[symbol, state] : symbol_event_state_)
    {
        auto &pending_events = state.pending_events;

        // Process events from this symbol's priority queue
        while (!pending_events.empty())
        {
            const auto &top_event = pending_events.top();

            bool ready = force;
            if (!force)
            {
                // Condition 1: Event is at least 1 second older than last dispatched event
                bool safe_by_time_gap = (state.last_event_time > 0) &&
                                        (top_event.event_time + utils::ONE_SECOND_NS <= state.last_event_time);

                // Condition 2: Event has been in queue for more than 1 second (timeout)
                uint64_t time_in_queue = now - top_event.received_time;
                bool safe_by_timeout = time_in_queue >= utils::ONE_SECOND_NS;
                ready = safe_by_time_gap || safe_by_timeout;
            }

            // Release if either condition is met
            if (ready)
            {
                // Copy event before popping (since top() returns const ref)
                Event event = top_event;
                pending_events.pop();

                // Dispatch the event
                dispatchEvent(symbol, event, state);

                // if (!level_update_buffer_.empty()) {
                //     publishLevelUpdate(
                //         symbol->symbol_.c_str(),
                //         level_update_buffer_
                //     );
                //     level_update_buffer_.clear();
                // }

                // if (!trade_update_buffer_.empty()) {
                //     publishMDTrades(symbol->symbol_.c_str(), trade_update_buffer_);
                //     trade_update_buffer_.clear();
                // }
                state.last_sequence_id = event.sequence_id;
                state.last_seq_num = event.seq_num;
            }
            else
            {
                // Top event not ready yet, stop processing this symbol
                break;
            }
        }
    }
}

void CoinbaseExchange::dispatchEvent(Symbol *symbol, const Event &event, SymbolEventState &state)
{
    if (event.type == EventType::LEVEL_UPDATE)
    {
        // Ignore stale events - skip if older than last update
        if (event.event_time < symbol->order_book_->lastUpdateTime())
        {
            LOG_WARN("[CoinbaseExchange] Skipping stale level update for symbol {}: seq_num={}, event_time={}({}), last_update_time={}({}) last_seq_num={}",
                     symbol->symbol_,
                     event.seq_num,
                     utils::format_timestamp_iso8601(event.event_time), event.event_time,
                     utils::format_timestamp_iso8601(symbol->order_book_->lastUpdateTime()), symbol->order_book_->lastUpdateTime(), symbol->order_book_->lastSeqNum());
            return;
        }

        // Update last update time before processing
        // TODO: add fantom order then match

        // auto book_side = to_book_side(side);
        // if (is_snapshot) {
        //     feed_md_level_quantity_[price] = size;
        //     // Create a phantom order to represent the new L2 level
        //     addOrder(utils::nextOrderId(), book_side, price, size, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
        //     auto [level, index] = getLevel(book_side, price);
        //     return std::make_tuple(index, level->total_quantity, static_cast<uint32_t>(level->orderCount()));
        // }
        auto &book = symbol->order_book_;

        auto book_side = to_book_side(event.side);
        auto [level, index] = symbol->order_book_->getLevel(book_side, event.price);
        auto action = MDUpdateAction::ACTION_CHANGE;
        if (!level)
        {
            // this is a new level
            qty_t qty = event.qty;
            auto order_id = utils::nextOrderId();
            auto trade_summaries = symbol->matching_engine_->match(event.side, order_id, event.price, qty, *book.get(), event.event_time, event.seq_num);
            if (qty)
            {
                symbol->order_book_->addOrder(order_id, book_side, event.price, qty, event.event_time, event.seq_num, event.flags & UpdateFlags::F_END_EVENT);
                action = MDUpdateAction::ACTION_NEW;
            }

            if (!trade_summaries.empty())
            {
                for (const auto &summary : trade_summaries)
                {
                    publishTradeSummary(symbol->symbol_.c_str(), summary);
                }
            }

            if (!symbol->md_level_update_cache_.empty())
            {
                publishLevelUpdate(symbol->symbol_.c_str(), symbol->md_level_update_cache_);
                symbol->md_level_update_cache_.clear();
            }

            if (!symbol->md_order_update_cache_.empty())
            {
                // TODO: publish MD order update
                symbol->md_order_update_cache_.clear();
            }
        }
        else
        {
            // existing level
            auto md_level_qty = symbol->order_book_->getMDLevelQty(event.price);
            if (event.qty > md_level_qty)
            {
                // Level qty increased, assume new orders added at the back. Create new phantom order to represent then increment
                auto size = event.qty - md_level_qty;
                symbol->order_book_->addBookOrder(utils::nextOrderId(), book_side, event.price, size, event.event_time, event.seq_num);
            }
            else if (event.qty < md_level_qty)
            {
                // Level qty decreased. Alwayse assume the worst case, that the quantity decreased at the back
                auto diff = md_level_qty - event.qty;
                for (auto it = level->orders.rbegin(), it_last = std::prev(level->orders.rend()); it != level->orders.rend(); ++it)
                {
                    auto &order = *it;
                    auto *exch_order = symbol->findOrder(order.order_id);
                    if (exch_order)
                    {
                        // This is an exchange order, skip it
                        continue;
                    }

                    auto to_reduce = std::min<qty_t>(diff, order.quantity);
                    symbol->order_book_->modifyOrder(order.order_id, event.price, order.quantity - to_reduce, event.event_time, order.priority, event.seq_num, (event.flags & UpdateFlags::F_END_EVENT) && (diff == 0));
                    diff -= to_reduce;
                    if (diff == 0 || symbol->order_book_->getMDLevelQty(event.price) == 0)
                    {
                        break;
                    }
                }
            }
        }

        symbol->order_book_->setLastUpdate(event.event_time, event.seq_num);

        if (!state.last_published_level_seq_num) [[unlikely]]
        {
            state.last_published_level_seq_num = event.seq_num;
        }

        if (!level_update_buffer_.empty() &&
            event.seq_num != state.last_published_level_seq_num)
        {
            publishLevelUpdate(
                symbol->symbol_.c_str(),
                level_update_buffer_);
            state.last_published_level_seq_num = level_update_buffer_.back().seq_num;
            level_update_buffer_.clear();
        }

        auto [new_level, new_index] = symbol->order_book_->getLevel(book_side, event.price);
        if (!level && new_level)
        {
            index = new_index;
        }
        else if (!new_level)
        {
            action = MDUpdateAction::ACTION_DELETE;
        }

        level_update_buffer_.emplace_back(MDLevel{
            .event_time = event.event_time,
            .seq_num = event.seq_num,
            .price = event.price,
            .qty = event.qty,
            .num_orders = 0,
            .level_index = index,
            .update_action = action,
            .flags = event.flags,
            .side = event.side});
    }
    else if (event.type == EventType::TRADE)
    {
        // TODO: Match against orders in the order book via matching engine
        // LOG_DEBUG("[CoinbaseExchange] Trade: {} {} @ {} size={} time={} seq_num={} last_published_level_seq_num={}",
        //           symbol->symbol_,
        //           event.side == Side::BUY ? "BUY" : "SELL",
        //           event.price,
        //           event.qty,
        //           event.event_time,
        //           event.seq_num,
        //           state.last_published_level_seq_num);

        if (!trade_update_buffer_.empty() &&
            state.last_published_trade_seq_num &&
            event.seq_num != state.last_published_trade_seq_num)
        {
            publishMDTrades(
                symbol->symbol_.c_str(),
                trade_update_buffer_);
            state.last_published_trade_seq_num = trade_update_buffer_.back().seq_num;
            trade_update_buffer_.clear();
        }

        trade_update_buffer_.emplace_back(MDTrade{
            .event_time = event.event_time,
            .seq_num = event.seq_num,
            .price = event.price,
            .qty = event.qty,
            .flags = event.flags,
            .side = event.side});
    }
}

void CoinbaseExchange::populateMDTradesResponse(Symbol *symbol)
{
    uint32_t num_trades = 0;
    auto it = trade_snapshots_.find(symbol);
    if (it != trade_snapshots_.end())
    {
        num_trades = static_cast<uint32_t>(it->second.size());
    }

    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(MDTradeUpdate) + num_trades * sizeof(MDTrade)); // Empty snapshot
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    memcpy(update->symbol, symbol->symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse *>(update->data);
    response->channel = coinbase::WebSocketChannel::MARKET_TRADES;

    auto *snapshot = reinterpret_cast<MDTradeUpdate *>(response->data);
    snapshot->num_trades = num_trades;
    auto *trades = reinterpret_cast<MDTrade *>(snapshot->trades);

    if (it != trade_snapshots_.end())
    {
        // Coinbase trades are in reverse chronological order
        for (auto i = num_trades - 1; i < num_trades; i--)
        {
            const auto *evt = it->second[i];
            if (evt == nullptr) [[unlikely]]
            {
                continue;
            }
            trades->event_time = evt->event_time;
            trades->price = evt->price;
            trades->qty = evt->qty;
            trades->side = evt->side;
            trades++;
        }
    }
    md_queue_.publish(index, sz);
}