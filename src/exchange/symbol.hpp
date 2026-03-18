#pragma once

#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <slick/orderbook/observer.hpp>

namespace slick::sim::exch {

struct Symbol : public orderbook::IOrderBookObserver, public std::enable_shared_from_this<Symbol> {
    symid_t id_ = INVALID_SYMBOL_ID;
    Venue venue_;
    std::string symbol_;
    engine::MatchingEngine *matching_engine_;
    std::unique_ptr<OrderBook> order_book_;
    std::atomic_uint16_t num_subscriptions_ = 0;
    SelfMatchPreventionMode smp_mode_ = SelfMatchPreventionMode::NONE;
    std::vector<MDLevel> md_level_update_cache_;
    std::vector<MDOrder> md_order_update_cache_;

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

    template<bool IS_LEVEL2>
    void createOrderBook() {
        order_book_ = std::make_unique<OrderBookImpl<IS_LEVEL2>>(id_, symbol_, venue_);
        order_book_->addObserver(shared_from_this());
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

    void onPriceLevelUpdate(const PriceLevelUpdate& update) override {
        // md_level_update_cache_.push_back({
        //     .event_time = update.timestamp,
        //     .seq_num = update.seq_num,
        //     .price = update.price,
        //     .quantity = update.quantity,
        //     .num_orders = update.num_orders,
        //     .flags = static_cast<>(update.flags),
        //     .side = static_cast<Side>(update.side)
        // });
    }

    /// Called when an individual order is updated (L3 event)
    void onOrderUpdate(const OrderUpdate& update) override {
        // md_order_update_cache_.push_back(update.order);
    }
};



inline std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::addOrder(Order* order)
{
    LOG_INFO("{} Adding order {}: oid={}, client_oid={}, price={}, qty={}, tif={}",
        symbol_, order->id, order->order_id, order->client_order_id,
        order->price, order->quantity, static_cast<char>(order->time_in_force));

    // Pass Symbol's SMP mode to matching engine
    auto [reject_reason, trade_summaries] = matching_engine_->match(
        order, order->price, order->quantity, *order_book_.get(),
        order->created_time.time_since_epoch().count(), 0, smp_mode_
    );

    // Only add to book if no rejection and remaining quantity exists
    if (reject_reason == OrdRejectReason::NONE && order->leaves_quantity > 0) {
        // Check if TimeInForce allows resting on book
        bool should_rest = (
            order->time_in_force == TimeInForce::DAY ||
            order->time_in_force == TimeInForce::GOOD_TILL_CANCEL ||
            order->time_in_force == TimeInForce::GOOD_TILL_DATE
        );

        if (should_rest) {
            order_book_->addOrder(order, order->created_time.time_since_epoch().count(), 0, true);
        } else {
            // IOC: Remainder is implicitly cancelled (not added to book)
            // FOK: Will never reach here (would have been rejected in match)
            LOG_INFO("{} Order {} has TIF={}, not adding to book (leaves_qty={})",
                symbol_, order->id, static_cast<char>(order->time_in_force), order->leaves_quantity);
        }
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