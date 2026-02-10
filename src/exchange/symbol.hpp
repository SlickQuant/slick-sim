#pragma once

#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <memory>
#include <tuple>
#include <unordered_set>

namespace slick::sim::exch {

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

    std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> addOrder(const AddOrderMessage &msg);
    std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> modifyOrder(const ModifyOrderMessage &msg);
    std::tuple<OrdRejectReason, Order*> cancelOrder(const CancelOrderMessage &msg);
};



inline std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> Symbol::addOrder(const AddOrderMessage &msg)
{
    std::vector<TradeSummary> trades;
    return std::make_tuple(OrdRejectReason::NONE, nullptr, trades);
    // return matching_engine_->addOrder(*order_book_.get(), msg);
}

inline std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> Symbol::modifyOrder(const ModifyOrderMessage &msg)
{
    std::vector<TradeSummary> trades;
    return std::make_tuple(OrdRejectReason::NONE, nullptr, trades);
    // return matching_engine_->modifyOrder(*order_book_.get(), msg);
}

inline std::tuple<OrdRejectReason, Order*> Symbol::cancelOrder(const CancelOrderMessage &msg)
{
    return std::make_tuple(OrdRejectReason::NONE, nullptr);
    // return matching_engine_->cancelOrder(*order_book_.get(), msg);
}

}