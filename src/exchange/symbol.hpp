#pragma once

#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <memory>
#include <tuple>
#include <unordered_set>

namespace slick::sim::exch {

using TradeSummaryInfo = engine::MatchingEngine::TradeSummaryInfo;

struct Symbol {
    symid_t id_ = INVALID_SYMBOL_ID;
    Venue venue_;
    std::string symbol_;
    engine::MatchingEngine *matching_engine_;
    std::unique_ptr<OrderBook> order_book_;
    std::atomic_uint16_t num_subscriptions_ = 0;

    Symbol() = default;
    ~Symbol() = default;

    Symbol(const Symbol&) = delete;
    Symbol& operator= (const Symbol&) = delete;

    Symbol(Symbol&& other) noexcept
        : id_(std::exchange(other.id_, 0))
        , venue_(std::exchange(other.venue_, Venue::UNKNOWN_VENUE))
        , symbol_(std::move(other.symbol_))
        , matching_engine_(std::exchange(other.matching_engine_, nullptr))
        , order_book_(std::move(other.order_book_))
        , num_subscriptions_(other.num_subscriptions_.load(std::memory_order_relaxed))
    {}
    Symbol& operator= (Symbol&& other) {
        if (this != &other) {
            id_ = std::exchange(other.id_, 0);
            venue_ = std::exchange(other.venue_, Venue::UNKNOWN_VENUE);
            symbol_ = std::move(other.symbol_);
            matching_engine_ = std::exchange(other.matching_engine_, nullptr);
            order_book_ = std::move(other.order_book_);
            num_subscriptions_.store(other.num_subscriptions_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> addOrder(Order *order);
    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> modifyOrder(Order* order, price_t new_price, qty_t new_qty);
    void cancelOrder(Order *order);

    Order* findOrderByClientOrderId(const char* client_order_id) const {
        return order_book_->findOrderByClientOrderId(client_order_id);
    }

    Order* findOrderByOrderId(const char* order_id) const {
        return order_book_->findOrderByOrderId(order_id);
    }

    Order* findOrder(uint64_t order_id) const {
        return order_book_->findOrder(order_id);
    }
};



inline std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::addOrder(Order* order)
{
    LOG_INFO("{} Adding order {}: oid={}, client_oid={}, price={}, qty={}", symbol_, order->id, order->order_id, order->client_order_id, order->price, order->quantity);
    auto [reject_reason, trade_summaries] = matching_engine_->match(order, order->price, order->quantity, *order_book_.get(), order->created_time.time_since_epoch().count());
    if (reject_reason == OrdRejectReason::NONE && order->leaves_quantity > 0) {
        order_book_->addOrder(order, order->created_time.time_since_epoch().count(), 0, true);
    }
    return std::make_tuple(reject_reason, trade_summaries);
}

inline std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::modifyOrder(Order* order, price_t new_price, qty_t new_qty)
{
    LOG_INFO("{} Modifying order {}: oid={}, client_oid={}, new_price={}, new_qty={}", symbol_, order->id, order->order_id, order->client_order_id, new_price, new_qty);
    
    auto [reject_reason, trade_summaries] = matching_engine_->match(order, new_price, new_qty, *order_book_.get(), order->last_update_time.time_since_epoch().count());
    
    if (reject_reason == OrdRejectReason::NONE) {
        order_book_->modifyOrder(order, new_price, new_qty, order->last_update_time.time_since_epoch().count(), 0, true);
    }
    
    order->last_update_time = std::chrono::system_clock::now();
    return std::make_tuple(reject_reason, trade_summaries);
}

inline void Symbol::cancelOrder(Order *order)
{
    LOG_INFO("{} Canceling order: oid={}, client_oid={}", symbol_, order->order_id, order->client_order_id);
    order->last_update_time = std::chrono::system_clock::now();
    order_book_->deleteOrder(order, order->last_update_time.time_since_epoch().count(), 0, true);
}

} // namespace exchange