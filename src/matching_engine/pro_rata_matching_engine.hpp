#pragma once

#include "matching_engine.hpp"
#include "matching_config.hpp"
#include <common/order.hpp>
#include <vector>

namespace slick::sim::engine {

/// Pro-rata matching: at each price level an aggressor that cannot take the whole
/// level is split across its orders by size, not by queue position.
///
/// Per level, for incoming quantity Q against orders of total T:
///   - Q >= T fills every order, as FIFO would.
///   - Otherwise `fifo_percent` of Q (rounded down to lots) goes FIFO first,
///     then the rest is split in proportion to each order's remaining size,
///     rounded down to lots, with shares under `min_allocation` dropped to 0,
///     and whatever is left goes FIFO with no minimum.
/// Each resting order gets one fill per aggressor per level, in queue order.
///
/// One instance serves every symbol of a venue and is only ever called from the
/// exchange thread, so its scratch buffer needs no synchronisation.
class ProRataMatchingEngine : public MatchingEngine {
public:
    /// One resting order's share of an aggressor at a level.
    struct Allocation {
        uint64_t order_id;
        qty_t size;      ///< resting quantity before the fill
        qty_t qty;       ///< allocated quantity, 0 when the order gets nothing
        Order *order;    ///< the simulator's Order; resolved only when SMP needs it
    };

    ProRataMatchingEngine(slick::queue<OrderResponse> &response_queue, const ProRataConfig &config = {});
    ~ProRataMatchingEngine() override = default;

    MatchingEngine::Type type() const noexcept override {
        return MatchingEngine::Type::ProRata;
    }

    const ProRataConfig &config() const noexcept {
        return config_;
    }

    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, time_t request_time, time_t event_time, uint64_t seq_num = 0, SelfMatchPreventionMode smp_mode = SelfMatchPreventionMode::NONE) override;
    std::vector<TradeSummaryInfo> match(Side side, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, time_t event_time, uint64_t seq_num = 0) override;

private:
    template<Side SIDE>
    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> matchOrder(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, time_t request_time, time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode);
    template<Side SIDE>
    std::vector<TradeSummaryInfo> matchFeed(uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, time_t event_time, uint64_t seq_num);

    /// Splits `qty` across the level's orders into `allocations_`, in queue order.
    /// `resolve` also looks up each order's Order in `book`, for the SMP check;
    /// without it `book` is not touched and `Allocation::order` stays nullptr.
    void allocate(const slick::orderbook::detail::PriceLevelL3 &level, qty_t qty, OrderBook &book, bool resolve);

    qty_t floorLot(qty_t qty) const noexcept {
        return qty - qty % config_.lot_size;
    }

    ProRataConfig config_;
    /// Reused across calls so matching allocates nothing once warm. Allocations
    /// are computed for a whole level before any is executed: a full fill unlinks
    /// the order from the level's intrusive list.
    std::vector<Allocation> allocations_;
};

}   // namespace slick::sim::engine
