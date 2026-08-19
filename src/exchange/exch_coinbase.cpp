#include <slick/logger.hpp>
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
    assert(!sym_mgr.getSymbol(product_id, venue_));
    auto symbol = sym_mgr.createSymbol(product_id, Venue::COINBASE);
    assert(symbol);
    if (!matching_engines_[engine::MatchingEngine::Type::FIFO])
    {
        matching_engines_[engine::MatchingEngine::Type::FIFO] = std::make_unique<engine::FifoMatchingEngine>(response_queue_);
    }
    symbol->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
    symbol->smp_mode_ = smp_mode_;
    symbol->createOrderBook<OrderBookType::L2>();
    return symbol;
}

void CoinbaseExchange::handleMdSubscription(const Request &request)
{
    const auto &msg = request.md_subscription;
    std::string sym = request.symbol;
    auto *symbol = sym_mgr.getSymbol(sym, venue_);
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
    auto *symbol = sym_mgr.getSymbol(sym, venue_);
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
                     symbol->symbol_.view(),
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
                    publishTradeSummary(symbol, summary);
                }
            }

            if (!symbol->md_level_update_cache_.empty())
            {
                publishLevelUpdate(symbol->symbol_, symbol->md_level_update_cache_);
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
            reconcilePhantomQty(symbol, event.side, event.price, event.qty, event.event_time,
                                event.seq_num, event.flags & UpdateFlags::F_END_EVENT);
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
                symbol->symbol_,
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

        // Publish the level as the book actually holds it, not the venue's raw
        // figure. Two reasons they diverge:
        //
        //  - The book may decline to take the update at face value:
        //    reconcilePhantomQty lowers a stale update's target by the quantity a
        //    trade already removed, and the new-level branch above rests only what
        //    survived matching.
        //  - total_quantity includes the simulator's own resting orders. A real
        //    venue's L2 shows your orders too, and both other producers of level
        //    data agree - Symbol::onPriceLevelUpdate forwards the book's level
        //    quantity, and populateL2Snapshot publishes total_quantity.
        //
        // Emitting event.qty would leave a client's incrementally-maintained book
        // disagreeing with the simulator's own, and with any snapshot it re-reads.
        auto published_qty = new_level ? new_level->total_quantity : qty_t{0};
        auto published_orders = new_level ? static_cast<uint32_t>(new_level->orderCount()) : 0u;

        level_update_buffer_.emplace_back(MDLevel{
            .event_time = event.event_time,
            .seq_num = event.seq_num,
            .price = event.price,
            .qty = published_qty,
            .num_orders = published_orders,
            .level_index = index,
            .update_action = action,
            .flags = event.flags,
            .side = event.side});
    }
    else if (event.type == EventType::TRADE)
    {
        // The venue's print is replayed as an incoming aggressor order: it takes
        // liquidity from the book exactly as the real taker did, filling any
        // simulator order that was ahead of the phantom liquidity it swept. Only
        // the resulting fills are published - the print itself is never relayed.
        auto &book = *symbol->order_book_;
        auto book_side = to_book_side(event.side);
        auto order_id = utils::nextOrderId();

        qty_t qty = event.qty;
        auto trade_summaries = symbol->matching_engine_->match(
            event.side, order_id, event.price, qty, book, event.event_time, event.seq_num);

        if (qty)
        {
            // Treated as an aggressor order, so the unfilled remainder rests -
            // same as the new-level branch above, and with the same order id. It
            // cannot cross: a remainder exists precisely because nothing was
            // still crossable at this limit.
            book.addOrder(order_id, book_side, event.price, qty, event.event_time, event.seq_num,
                          event.flags & UpdateFlags::F_END_EVENT);
        }

        for (const auto &summary : trade_summaries)
        {
            // The venue will report the same reduction again in its level update
            // for this trade; credit it so that update does not remove it twice.
            symbol->creditTradedQty(opposite_side(event.side), summary.price,
                                    summary.phantom_qty, event.event_time);
            publishTradeSummary(symbol, summary);
        }

        if (!symbol->md_level_update_cache_.empty())
        {
            publishLevelUpdate(symbol->symbol_, symbol->md_level_update_cache_);
            symbol->md_level_update_cache_.clear();
        }

        if (!symbol->md_order_update_cache_.empty())
        {
            // TODO: publish MD order update
            symbol->md_order_update_cache_.clear();
        }
    }
}

void CoinbaseExchange::populateMDTradesResponse(Symbol *symbol)
{
    publishTradeSubscriptionResponse(symbol, coinbase::WebSocketChannel::MARKET_TRADES);
}