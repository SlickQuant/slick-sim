#pragma once

#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <slick/orderbook/observer.hpp>

namespace slick::sim::exch {

struct Symbol;

struct OrderBookObserver : public orderbook::IOrderBookObserver {
    Symbol* symbol_;
    OrderBookObserver(Symbol* symbol) : symbol_(symbol) {}

    void onPriceLevelUpdate(const PriceLevelUpdate& update) override;

    void onOrderUpdate(const OrderUpdate& update) override;
};

struct Symbol {
    symid_t id_ = INVALID_SYMBOL_ID;
    Venue venue_;
    std::string symbol_;
    engine::MatchingEngine *matching_engine_;
    std::shared_ptr<OrderBook> order_book_;
    std::atomic_uint16_t num_subscriptions_ = 0;
    SelfMatchPreventionMode smp_mode_ = SelfMatchPreventionMode::NONE;
    std::vector<MDLevel> md_level_update_cache_;
    std::vector<MDOrder> md_order_update_cache_;
    std::shared_ptr<OrderBookObserver> book_observer_;

    Symbol() : book_observer_(std::make_shared<OrderBookObserver>(this)) {
        md_level_update_cache_.reserve(64);
        md_order_update_cache_.reserve(256);
    };
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
        , book_observer_(std::make_shared<OrderBookObserver>(this))
    {
        if (order_book_) {
            order_book_->removeObserver(other.book_observer_);
            order_book_->addObserver(book_observer_);
        }
    }
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

    template<OrderBookType BookType>
    void createOrderBook() {
        order_book_ = std::make_shared<OrderBookImpl<BookType>>(id_, symbol_, venue_);
        order_book_->addObserver(order_book_->shared_from_this());
        order_book_->addObserver(book_observer_);
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

    void onPriceLevelUpdate(const PriceLevelUpdate& update);

    /// Called when an individual order is updated (L3 event)
    void onOrderUpdate(const OrderUpdate& update);
};


} // namespace exchange