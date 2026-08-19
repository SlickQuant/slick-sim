#pragma once

#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <utils/ring_buffer.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <tuple>
#include <unordered_map>
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

/// Quantity one trade print removed from phantom liquidity at one price, kept
/// until a level update stamped at or after it accounts for it. Credits are held
/// per trade timestamp, never summed across timestamps: a level update reflects
/// only the trades that preceded it. See Symbol::creditTradedQty.
struct TradedCredit {
    qty_t qty = 0;
    time_t event_time = 0;
};

struct Symbol {
    /// Recent trades published on this instrument's tape, newest at the back.
    /// Serves the subscribe-time snapshot on every venue. Capacity must stay a
    /// power of two - utils::RingBuffer asserts on it.
    static constexpr size_t TRADE_HISTORY_CAPACITY = 64;

    symid_t id_ = INVALID_SYMBOL_ID;
    Venue venue_;
    /// Wire-ready, so the publish path copies it as a fixed block. As a
    /// std::string it was copied with
    /// `memcpy(update->symbol, symbol_.c_str(), sizeof(update->symbol))`, which
    /// reads past the end of every name shorter than the wire field.
    symbol_name_t symbol_;
    engine::MatchingEngine *matching_engine_;
    std::shared_ptr<OrderBook> order_book_;
    std::atomic_uint16_t num_subscriptions_ = 0;
    SelfMatchPreventionMode smp_mode_ = SelfMatchPreventionMode::NONE;
    std::vector<MDLevel> md_level_update_cache_;
    std::vector<MDOrder> md_order_update_cache_;
    std::shared_ptr<OrderBookObserver> book_observer_;
    utils::RingBuffer<MDTrade> trade_history_{TRADE_HISTORY_CAPACITY};

    /// Both caches are filled by book observers and drained by the exchange
    /// thread, which only ever `clear()`s them, so the capacity reserved here is
    /// the capacity they keep for the process's lifetime. Sized for the largest
    /// batch a busy instrument produces in one feed message so a market-data
    /// flood never reaches the allocator.
    Symbol() : book_observer_(std::make_shared<OrderBookObserver>(this)) {
        md_level_update_cache_.reserve(256);
        md_order_update_cache_.reserve(1024);
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
        // Moved, not left default-constructed: a Symbol that is moved must arrive
        // with the capacity the constructor reserved, or it runs the rest of its
        // life reallocating both caches as market data arrives. SymbolManager now
        // constructs in place and never moves one, so nothing in the simulator
        // exercises this today - which is exactly why it is worth stating.
        , md_level_update_cache_(std::move(other.md_level_update_cache_))
        , md_order_update_cache_(std::move(other.md_order_update_cache_))
        , book_observer_(std::make_shared<OrderBookObserver>(this))
        , trade_history_(std::move(other.trade_history_))
        , traded_qty_credit_(std::move(other.traded_qty_credit_))
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
            md_level_update_cache_ = std::move(other.md_level_update_cache_);
            md_order_update_cache_ = std::move(other.md_order_update_cache_);
            trade_history_ = std::move(other.trade_history_);
            traded_qty_credit_ = std::move(other.traded_qty_credit_);
        }
        return *this;
    }

    /// Records that a trade print consumed `phantom_consumed` of the phantom
    /// liquidity resting at (side, price).
    ///
    /// The venue will report the same reduction again in its own level update
    /// for that trade. Applying both would reduce the level twice, so the level
    /// path offsets against this credit when the update predates the trade.
    /// Only phantom quantity is credited: when a trade fills the simulator's own
    /// resting order the venue's level still legitimately drops by that amount,
    /// because that liquidity really did trade at the venue.
    void creditTradedQty(Side side, price_t price, qty_t phantom_consumed, time_t event_time) {
        if (phantom_consumed <= 0 || side > Side::SELL) {
            return;
        }
        auto &credits = traded_qty_credit_[static_cast<size_t>(side)][price];
        // One credit per timestamp. A single print's sweep of one level arrives as
        // one summary, but a batch can carry several prints stamped alike.
        for (auto &credit : credits) {
            if (credit.event_time == event_time) {
                credit.qty += phantom_consumed;
                return;
            }
        }
        credits.emplace_back(phantom_consumed, event_time);
    }

    /// Returns the quantity traded at (side, price) that a level update stamped
    /// `as_of` does not yet reflect, and drops every credit it does reflect.
    ///
    /// Credits are resolved individually rather than summed: an update landing
    /// between two trades at the same price reflects the earlier one and not the
    /// later one, so offsetting by both would over-reduce the level. Credits it
    /// does not reflect are kept for the next update to resolve; the ones it does
    /// are dropped, so stale credit can never survive to swallow a genuine cancel.
    qty_t takeTradedQty(Side side, price_t price, time_t as_of) {
        if (side > Side::SELL) {
            return 0;
        }
        auto &by_price = traded_qty_credit_[static_cast<size_t>(side)];
        auto it = by_price.find(price);
        if (it == by_price.end()) {
            return 0;
        }

        auto &credits = it->second;
        qty_t unreflected = 0;
        for (auto credit = credits.begin(); credit != credits.end();) {
            if (as_of < credit->event_time) {
                // The update predates this trade, so it still reports the
                // pre-trade quantity at this price.
                unreflected += credit->qty;
                ++credit;
            } else {
                credit = credits.erase(credit);
            }
        }
        if (credits.empty()) {
            by_price.erase(it);
        }
        return unreflected;
    }

    /// Appends a trade to the tape history that serves the subscribe snapshot.
    void recordTrade(const MDTrade &trade) {
        trade_history_.push(trade);
    }

    /// Drops the tape history, so a venue that is about to resend its own trade
    /// snapshot does not end up replaying the overlap twice.
    void clearTradeHistory() {
        trade_history_ = utils::RingBuffer<MDTrade>(TRADE_HISTORY_CAPACITY);
    }

    template<OrderBookType BookType>
    void createOrderBook() {
        order_book_ = std::make_shared<OrderBookImpl<BookType>>(id_, symbol_, venue_);
        order_book_->addObserver(order_book_->shared_from_this());
        order_book_->addObserver(book_observer_);
    }

    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> addOrder(Order *order, time_t request_time);
    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> modifyOrder(Order* order, price_t new_price, qty_t new_qty, time_t request_time);
    void cancelOrder(Order *order, time_t request_time);

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

private:
    /// price -> quantities trades already removed, one entry per trade timestamp,
    /// indexed by Side. Only BUY and SELL are ever indexed.
    std::array<std::unordered_map<price_t, std::vector<TradedCredit>>, 2> traded_qty_credit_;
};


} // namespace exchange