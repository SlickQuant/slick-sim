#include "exch_coinbase.hpp"

using namespace slick::sim::exch;
using namespace slick::sim;

namespace {
    auto &sym_mgr = SymbolManager::instance();
}

void CoinbaseExchange::onMarketDataConnected(WebSocketClient* client) {
    LOG_INFO("WebSocket {:p} market data connected", (void*)client);
}

void CoinbaseExchange::onMarketDataDisconnected(WebSocketClient* client) {
    LOG_INFO("WebSocket {:p} market data disonnected", (void*)client);
}

void CoinbaseExchagne::onUserDataConnected(WebsocketClient* client) {
    LOG_INFO("WebSocket {:p} user data connected", (void*)client);
}

void CoinbaseExchange::onUserDataDisconnected(WebSocketClient* client) {
    LOG_INFO("WebSocket {:p} user data disonnected", (void*)client);
}

void CoinbaseExchange::onLevel2Snapshot(WebSocketClient* client, uint64_t seq_num, const coinbase::Level2UpdateBatch& snapshot) {
    auto *symbol = sym_mgr.getSymbol(snapshot.product_id);
    if (!symbol) [[unlikely]] {
        return;
    }

    auto it = pending_l2_subscriptions.find(symbol);
    symbol->order_book_->clear();
    for (auto it = snapshot.updates.rbegin(), it_last = std::prev(snapshot.updates.rend()); it != snapshot.updates.rend(); ++it) {
        auto &update = *it;
        auto price = static_cast<price_t>(update.price_level * DOUBLE_MULTIPLIER);
        auto side = static_cast<Side>(update.side);
        auto qty = static_cast<qty_t>(update.new_quantity * DOUBLE_MULTIPLIER);
        symbol->order_book_->onLevelUpdate(update.event_time, update.seq_num, side, price, qty, it == it_last, true);
        // auto [index, qty, num_orders] = symbol->matching_engine_->onLevelUpdate(
        //     *symbol->order_book_,
        //     side,
        //     price,
        //     static_cast<qty_t>(update.new_quantity * DOUBLE_MULTIPLIER),
        //     0,
        //     level_update_buffer_,
        //     trade_update_buffer_
        // );

        if (it == pending_l2_subscriptions.end()) {
            level_update_buffer_.emplace_back(MDLevel{
                .event_time = update.event_time,
                .seq_num = seq_num,
                .price = price,
                .qty = qty,
                .num_orders = 1,
                .flags = LevelUpdateFlags::IS_SNAPSHOT | (it == it_last ? LevelUpdateFlags::END_EVENT : LevelUpdateFlags::NONE);
                .side = side
            });
        }

        if (update.event_time > symbol->order_book_->lastUpdateTime()) {
            symbol->order_book_->setLastUpdateTime(update.event_time);
        }
    }

    auto &pending_l2_subscriptions = pending_md_subscription_[coinbase::WebSocketChannel::LEVEL2];

    if (it != pending_l2_subscriptions.end()) {
        symbol->order_book_->populateL2SubscriptionResponse(md_update_queue_, coinbase::WebSocketChannel::LEVEL2);
        pending_l2_subscriptions.erase(it);
    }

    // publishMDBookUpdate(symbol->id_, *symbol->order_book_.get(), indices);
    LOG_DEBUG(symbol->order_book_->to_string());
}

void CoinbaseExchange::onLevel2Updates(WebSocketClient* client, uint64_t seq_num, const coinbase::Level2UpdateBatch& updates) {
    auto *symbol = sym_mgr.getSymbol(updates.product_id);
    if (!symbol) [[unlikely]] {
        return;
    }

    // Get or create event state for this symbol
    auto& state = symbol_event_state_[symbol];

    // Add each update to per-symbol pending_events queue
    for (auto it = snapshot.updates.rbegin(), it_last = std::prev(snapshot.updates.rend()); it != snapshot.updates.rend(); ++it) {
        auto &update = *it;
        Event evt;
        evt.type = EventType::LEVEL_UPDATE;
        evt.event_time = update.event_time;  // Use Coinbase timestamp
        evt.sequence_id = state.next_sequence_id++;  // Internal counter
        evt.seq_num = seq_num;
        evt.received_time = utils::get_current_time_ns();
        evt.price = static_cast<price_t>(update.price_level * DOUBLE_MULTIPLIER);
        evt.qty = static_cast<qty_t>(update.new_quantity * DOUBLE_MULTIPLIER);
        evt.side = static_cast<Side>(update.side);
        evt.is_last_in_batch = it == it_last;

        // Update last_event_time to track latest event_time seen
        if (state.last_event_time < evt.event_time) {
            state.last_event_time = evt.event_time;
        }

        // Add to per-symbol priority queue
        state.pending_events.push(std::move(evt));
    }
}

void CoinbaseExchange::onMarketTradesSnapshot(WebSocketClient* client, uint64_t seq_num, const std::vector<coinbase::MarketTrade>& snapshots) {
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

void CoinbaseExchange::onMarketTrades(WebSocketClient* client, uint64_t seq_num, const std::vector<coinbase::MarketTrade>& trades) {
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

void CoinbaseExchange::onMarketDataGap(WebSocketClient* client) {
    LOG_WARN("CoinbaseExchange Market Data Gap detected");
}
void CoinbaseExchange::onUserDataGap(WebSocketClient* client) {
    LOG_WARN("CoinbaseExchange User Data Gap detected");
}
void CoinbaseExchange::onMarketDataError(WebSocketClient* client, std::string &&err) {
    LOG_ERROR("Websocket {:p} onMarketDataError {}", (void*)client, std::move(err));
}

void CoinbaseExchange::onUserDataError(WebSocketClient* client, std::string &&err) {
    LOG_ERROR("Websocket {:p} onUserDataError {}", (void*)client, std::move(err));
}