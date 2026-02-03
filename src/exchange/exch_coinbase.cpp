#include "exch_coinbase.hpp"
#include <md_feed/coinbase_live_ws_feed.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <common/symbol_manager.hpp>
#include <coinbase/websocket.hpp>
#include <utils/timestamp.hpp>
#include <chrono>

using namespace slick::sim::exch;
using namespace slick::sim;

namespace {
    auto &sym_mgr = SymbolManager::instance();
}

CoinbaseExchange::CoinbaseExchange(
    const nlohmann::json &config,
    slick::SlickQueue<Request> &request_queue,
    slick::SlickQueue<OrderResponse> &order_response_queue,
    slick::SlickQueue<uint8_t> &md_update_queue
)
    : Exchange(Venue::COINBASE, config, request_queue, order_response_queue, md_update_queue)
    , coinbase::UserThreadWebsocketCallbacks()
{
    if (config_.contains("md_feeds")) {
        for (const auto &md_feed_config : config_["md_feeds"]) {
            std::string type = md_feed_config.value("type", "");
            if (type == "coinbase_live_ws") {
                use_live_feed_ = true;
            }
        }
    }
}

void CoinbaseExchange::run() {
    run_.store(true, std::memory_order_release);
    if (md_feed_) {
        md_feed_->start();
    }
    thread_ = std::thread([this]() {
        while (run_.load(std::memory_order_relaxed)) {
            if (md_feed_ || !md_feeds_.empty()) {
                processData();
            }
            processSequencedEvents();
            processRequest();
        }
    });
    if (md_feed_) {
        md_feed_->stop();
    }
}

Symbol* CoinbaseExchange::addSymbol(std::string_view product_id) {
    Symbol sym;
    sym.symbol_ = product_id;
    if (!matching_engines_[engine::MatchingEngine::Type::FIFO]) {
        matching_engines_[engine::MatchingEngine::Type::FIFO] = std::make_unique<engine::FifoMatchingEngine>(request_queue_, order_response_queue_);
    }
    sym.matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
    sym.order_book_ = std::make_unique<OrderBookImpl<L2OrderBookLevel>>(std::string(product_id), Venue::COINBASE);
    return sym_mgr.addSymbol(std::move(sym));
}

void CoinbaseExchange::onLevel2Snapshot(const coinbase::Level2UpdateBatch& snapshot) {
    auto *symbol = sym_mgr.getSymbol(snapshot.product_id);
    if (!symbol) [[unlikely]] {
        return;
    }

    symbol->order_book_->clear();
    for (auto & update : snapshot.updates) {
        auto price = static_cast<price_t>(update.price_level * DOUBLE_MULTIPLIER);
        auto side = static_cast<Side>(update.side);
        auto [index, qty, num_orders] = symbol->matching_engine_->onLevelUpdate(
            *symbol->order_book_,
            side,
            price,
            static_cast<qty_t>(update.new_quantity * DOUBLE_MULTIPLIER),
            0,
            level_update_buffer_,
            trade_update_buffer_
        );

        level_update_buffer_.emplace_back(MDLevel{
            .event_time = update.event_time,
            .price = price,
            .qty = qty,
            .num_orders = num_orders,
            .side = side
        });

        if (update.event_time > symbol->order_book_->lastUpdateTime()) [[unlikely]] {
            symbol->order_book_->setLastUpdateTime(update.event_time);
        }
    }

    auto &pending_l2_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2];

    auto it = pending_l2_subscriptions.find(symbol);
    if (it != pending_l2_subscriptions.end()) {
        symbol->order_book_->populateL2SubscriptionResponse(md_update_queue_, coinbase::WebSocketChannel::LEVEL2);
        pending_l2_subscriptions.erase(it);
    }

    // publishMDBookUpdate(symbol->id_, *symbol->order_book_.get(), indices);
    LOG_DEBUG(symbol->order_book_->to_string());
}

void CoinbaseExchange::onLevel2Updates(const coinbase::Level2UpdateBatch& updates) {
    auto *symbol = sym_mgr.getSymbol(updates.product_id);
    if (!symbol) [[unlikely]] {
        return;
    }

    // Get or create event state for this symbol
    auto& state = symbol_event_state_[symbol];

    // Add each update to per-symbol pending_events queue
    for (const auto& update : updates.updates) {
        Event evt;
        evt.type = EventType::LEVEL_UPDATE;
        evt.event_time = update.event_time;  // Use Coinbase timestamp
        evt.sequence_id = state.next_sequence_id++;  // Internal counter
        evt.received_time = utils::get_current_time_ns();
        evt.price = static_cast<price_t>(update.price_level * DOUBLE_MULTIPLIER);
        evt.qty = static_cast<qty_t>(update.new_quantity * DOUBLE_MULTIPLIER);
        evt.side = static_cast<Side>(update.side);

        // Update last_event_time to track latest event_time seen
        if (state.last_event_time < evt.event_time) {
            state.last_event_time = evt.event_time;
        }

        // Add to per-symbol priority queue
        state.pending_events.push(std::move(evt));
    }
}

void CoinbaseExchange::onMarketTradesSnapshot(const std::vector<coinbase::MarketTrade>& snapshots) {
    auto &pending_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES];
    if (pending_subscriptions.empty() || snapshots.empty()) {
        return;
    }
    Symbol* symbol = sym_mgr.getSymbol(snapshots[0].product_id);
    if (!symbol || !pending_subscriptions.contains(symbol)) {
        return;
    }

    auto it = trade_snapshots_.find(symbol);
    if (it == trade_snapshots_.end()) {
        it = trade_snapshots_.emplace(symbol, utils::RingBuffer<Event>(16)).first;
    }

    auto &trade_snapshot = it->second;

    auto sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(MDTradeUpdate) + snapshots.size() * sizeof(MDTrade);
    auto index = md_update_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue_[index]);
    memcpy(update->symbol, symbol->symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    response->channel = coinbase::WebSocketChannel::MARKET_TRADES;

    auto *snapshot = reinterpret_cast<MDTradeUpdate*>(response->data);
    snapshot->num_trades = snapshots.size();

    auto *trades = reinterpret_cast<MDTrade*>(snapshot->trades);

    // Coinbase trades are in reverse chronological order
    for (auto i = snapshots.size() - 1; i >= 0; i--) {
        const auto& trade = snapshots[i];
        trades->price = trade.price * DOUBLE_MULTIPLIER;
        trades->qty = trade.size * DOUBLE_MULTIPLIER;
        trades->side = static_cast<Side>(trade.side);

        // Store in trade history ring buffer
        Event evt;
        evt.type = EventType::TRADE;
        evt.event_time = trade.time;
        evt.sequence_id = i;  // Use index as sequence for snapshot
        evt.received_time = utils::get_current_time_ns();
        evt.price = trade.price * DOUBLE_MULTIPLIER;
        evt.qty = trade.size * DOUBLE_MULTIPLIER;
        evt.side = static_cast<Side>(trade.side);
        it->second.push(evt);
        trade_snapshot.emplace(std::move(evt));

        trades++;
    }
    for (const auto& trade : snapshots) {
        trades->price = trade.price * DOUBLE_MULTIPLIER;
        trades->qty = trade.size * DOUBLE_MULTIPLIER;
        trades->side = static_cast<Side>(trade.side);
        trades++;
    }
    md_update_queue_.publish(index, sz);
    pending_subscriptions.erase(symbol);
}

void CoinbaseExchange::onMarketTrades(const std::vector<coinbase::MarketTrade>& trades) {
    for (const auto& trade : trades) {
        auto *symbol = sym_mgr.getSymbol(trade.product_id);
        if (!symbol) [[unlikely]] {
            continue;
        }

        // Get or create event state for this symbol
        auto& state = symbol_event_state_[symbol];

        Event evt;
        evt.type = EventType::TRADE;
        evt.event_time = trade.time;
        evt.sequence_id = state.next_sequence_id++;  // Internal counter
        evt.received_time = utils::get_current_time_ns();
        evt.price = trade.price * DOUBLE_MULTIPLIER;
        evt.qty = trade.size * DOUBLE_MULTIPLIER;
        evt.side = static_cast<Side>(trade.side);

        // Update last_event_time to track latest event_time seen
        if (state.last_event_time < evt.event_time) {
            state.last_event_time = evt.event_time;
        }

        // Add to per-symbol priority queue
        state.pending_events.push(std::move(evt));
    }
}

void CoinbaseExchange::onMarketDataGap() {
    LOG_WARN("CoinbaseExchange Market Data Gap detected");
}
void CoinbaseExchange::onUserDataGap() {
    LOG_WARN("CoinbaseExchange User Data Gap detected");
}
void CoinbaseExchange::onMarketDataError(std::string err) {}
void CoinbaseExchange::onUserDataError(std::string err) {}

void CoinbaseExchange::handleMdSubscription(const Request &request) {
    const auto &msg = request.md_subscription;
    std::string sym = request.symbol;
    auto *symbol = sym_mgr.getSymbol(sym);
    if (!symbol) {
        if (use_live_feed_) {
            symbol = addSymbol(sym);
            switch(static_cast<coinbase::WebSocketChannel>(msg.channel)) {
                case coinbase::WebSocketChannel::LEVEL2: {
                    pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2].emplace(symbol);
                    break;
                }
                case coinbase::WebSocketChannel::MARKET_TRADES: {
                    pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES].emplace(symbol);
                    break;
                }
            }
            md_feeds_.emplace_back(std::make_shared<md_feed::CoinbaseLiveWSFeed>(this, std::vector<std::string>{sym}));
            md_feeds_.back()->start();
            map_symbol_feed_.emplace(sym, md_feeds_.back());
            symbol->num_subscriptions_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        else {
            // TODO: reject
        }
    }
    symbol->num_subscriptions_.fetch_add(1, std::memory_order_relaxed);
    switch(static_cast<coinbase::WebSocketChannel>(msg.channel)) {
        case coinbase::WebSocketChannel::LEVEL2: {
            auto &pending_l2_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2];
            if (pending_l2_subscriptions.find(symbol) == pending_l2_subscriptions.end()) {
                // subscription already received, publish snapshot immediately
                symbol->order_book_->populateL2SubscriptionResponse(md_update_queue_, msg.channel);
            }
            else {
                // Already pending, do nothing
            }
            break;
        }
        case coinbase::WebSocketChannel::MARKET_TRADES: {
            auto &pending_trade_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::MARKET_TRADES];
            if (pending_trade_subscriptions.find(symbol) == pending_trade_subscriptions.end()) {
                // subscription already received, publish snapshot immediately
                populateMDTradesResponse(symbol);
            }
            else {
                // Already pending, do nothing
            }
            break;
        }
        case coinbase::WebSocketChannel::TICKER:
            break;
    }
}

void CoinbaseExchange::handleMdUnsubscription(const Request &request) {
    const auto &msg = request.md_subscription;
    std::string sym = request.symbol;
    auto *symbol = sym_mgr.getSymbol(sym);
    if (!symbol) {
        return;
    }
    if (symbol->num_subscriptions_.fetch_sub(1, std::memory_order_relaxed) == 1) {
        auto it_feed = map_symbol_feed_.find(sym);
        if (it_feed != map_symbol_feed_.end()) {
            auto feed = it_feed->second;
            feed->stop();
            md_feeds_.erase(std::remove(md_feeds_.begin(), md_feeds_.end(), feed), md_feeds_.end());
            map_symbol_feed_.erase(it_feed);
        }
    }
}

void CoinbaseExchange::processSequencedEvents() {
    const uint64_t now = utils::get_current_time_ns();

    // Process events from all symbols
    for (auto& [symbol, state] : symbol_event_state_) {
        auto& pending_events = state.pending_events;

        // Process events from this symbol's priority queue
        while (!pending_events.empty()) {
            const auto& top_event = pending_events.top();

            // Condition 1: Event is at least 1 second older than last dispatched event
            bool safe_by_time_gap = (state.last_event_time > 0) &&
                                    (top_event.event_time + utils::ONE_SECOND_NS <= state.last_event_time);

            // Condition 2: Event has been in queue for more than 1 second (timeout)
            uint64_t time_in_queue = now - top_event.received_time;
            bool safe_by_timeout = time_in_queue >= utils::ONE_SECOND_NS;

            // Release if either condition is met
            if (safe_by_time_gap || safe_by_timeout) {
                // Copy event before popping (since top() returns const ref)
                Event event = top_event;
                pending_events.pop();

                // Dispatch the event
                dispatchEvent(symbol, event);

                if (!level_update_buffer_.empty()) {
                    publishLevelUpdate(
                        symbol->symbol_.c_str(),
                        level_update_buffer_
                    );
                    level_update_buffer_.clear();
                }

                if (!trade_update_buffer_.empty()) {
                    publishMDTrades(symbol->symbol_.c_str(), trade_update_buffer_);
                    trade_update_buffer_.clear();
                }

            } else {
                // Top event not ready yet, stop processing this symbol
                break;
            }
        }
    }
}

void CoinbaseExchange::dispatchEvent(Symbol* symbol, const Event& event) {
    if (event.type == EventType::LEVEL_UPDATE) {
        // Ignore stale events - skip if older than last update
        if (event.event_time < symbol->order_book_->lastUpdateTime()) {
            LOG_WARN("[CoinbaseExchange] Skipping stale level update for symbol {}: event_time={}, last_update_time={}",
                      symbol->symbol_,
                      event.event_time,
                      symbol->order_book_->lastUpdateTime());
            return;
        }

        // Update last update time before processing
        symbol->order_book_->setLastUpdateTime(event.event_time);

        // Process level update
        auto [index, qty, order_count] = symbol->matching_engine_->onLevelUpdate(
            *symbol->order_book_,
            event.side,
            event.price,
            event.qty,
            0,
            level_update_buffer_,
            trade_update_buffer_
        );

        level_update_buffer_.emplace_back(MDLevel{
            .event_time = event.event_time,
            .price = event.price,
            .qty = qty,
            .num_orders = order_count,
            .side = event.side
        });
    }
    else if (event.type == EventType::TRADE) {
        // TODO: Match against orders in the order book via matching engine
        LOG_DEBUG("[CoinbaseExchange] Trade: {} {} @ {} size={} time={}",
                  symbol->symbol_,
                  event.side == Side::BUY ? "BUY" : "SELL",
                  event.price,
                  event.qty,
                  event.event_time);
        trade_update_buffer_.emplace_back(MDTrade{
            .event_time = event.event_time,
            .price = event.price,
            .qty = event.qty,
            .side = event.side
        });
    }
}

void CoinbaseExchange::populateMDTradesResponse(Symbol* symbol) {
    uint32_t num_trades = 0;
    auto it = trade_snapshots_.find(symbol);
    if (it != trade_snapshots_.end()) {
        num_trades = it->second.size();
    }

    auto sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(MDTradeUpdate) + num_trades * sizeof(MDTrade); // Empty snapshot
    auto index = md_update_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue_[index]);
    memcpy(update->symbol, symbol->symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    response->channel = coinbase::WebSocketChannel::MARKET_TRADES;

    auto *snapshot = reinterpret_cast<MDTradeUpdate*>(response->data);
    snapshot->num_trades = num_trades;
    auto *trades = reinterpret_cast<MDTrade*>(snapshot->trades);

    if (it != trade_snapshots_.end()) {
        // Coinbase trades are in reverse chronological order
        for (auto i = num_trades - 1; i < num_trades; i--) {
            const auto *evt = it->second[i];
            if (evt == nullptr) [[unlikely]] {
                continue;
            }
            trades->event_time = evt->event_time;
            trades->price = evt->price;
            trades->qty = evt->qty;
            trades->side = evt->side;
            trades++;
        }
    }
    md_update_queue_.publish(index, sz);
}