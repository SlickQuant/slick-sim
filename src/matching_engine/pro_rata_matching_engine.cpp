#include "pro_rata_matching_engine.hpp"
#include "match_common.hpp"
#include <utils/math.hpp>

using namespace slick::sim;
using namespace slick::sim::engine;
using namespace slick::sim::engine::detail;
using namespace slick::sim::utils;

namespace {

/// floor(a * b / c) on non-negative quantities, with b <= c.
inline qty_t share_of(qty_t a, qty_t b, qty_t c) noexcept {
    return static_cast<qty_t>(mul_div_floor(static_cast<uint64_t>(a), static_cast<uint64_t>(b), static_cast<uint64_t>(c)));
}

}   // namespace

ProRataMatchingEngine::ProRataMatchingEngine(slick::queue<OrderResponse> &response_queue, const ProRataConfig &config)
    : MatchingEngine(response_queue)
    , config_(config)
{
    // Not an assert: a zero lot divides by zero and a percentage over 100
    // over-allocates, and Release builds would carry on with either.
    config_.validate();
    allocations_.reserve(256);
}

void ProRataMatchingEngine::allocate(const slick::orderbook::detail::PriceLevelL3 &level, qty_t qty, OrderBook &book, bool resolve) {
    allocations_.clear();
    qty_t total = 0;
    for (const auto &book_order : level.orders) {
        allocations_.push_back({book_order.order_id, book_order.quantity, 0,
                                resolve ? book.findOrder(book_order.order_id) : nullptr});
        total += book_order.quantity;
    }

    if (qty >= total) {
        // The aggressor takes the whole level, so there is nothing to split.
        for (auto &a : allocations_) {
            a.qty = a.size;
        }
        return;
    }

    // From here qty < total, so every step below can place all it is given.
    qty_t left = qty;

    // 1. The FIFO slice.
    if (config_.fifo_percent > 0) {
        qty_t fifo = floorLot(share_of(qty, config_.fifo_percent, 100));
        left -= fifo;
        for (auto &a : allocations_) {
            if (fifo == 0) {
                break;
            }
            a.qty = std::min(fifo, a.size);
            fifo -= a.qty;
        }
    }

    // 2. Pro-rata on what each order still has, against what the level still has.
    if (left > 0) {
        const qty_t pro_rata = left;
        const qty_t base = total - (qty - left);
        for (auto &a : allocations_) {
            qty_t share = floorLot(share_of(pro_rata, a.size - a.qty, base));
            if (share < config_.min_allocation) {
                share = 0;
            }
            a.qty += share;
            left -= share;
        }
    }

    // 3. Whatever is left goes FIFO, with no minimum. On a level of many small
    //    orders every share can round to zero, so this can be most of `qty`.
    for (auto &a : allocations_) {
        if (left == 0) {
            break;
        }
        const qty_t extra = std::min(left, a.size - a.qty);
        a.qty += extra;
        left -= extra;
    }
    assert(left == 0);
}

template<Side SIDE>
std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> ProRataMatchingEngine::matchOrder(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, slick::sim::time_t request_time, slick::sim::time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    if (auto done = prepareOrder<SIDE, Reach::Level>(*this, order, order_price, order_qty, book, request_time, smp_mode)) {
        return std::make_tuple(*done, std::vector<TradeSummaryInfo>{});
    }

    MatchContext ctx{*this, book, event_time, seq_num};
    const bool check_self_match = smp_mode != SelfMatchPreventionMode::NONE;
    order_qty = order->leaves_quantity;
    bool abandoned = false;

    while (order_qty > 0 && !abandoned) {
        auto best_level = book.getBestLevel<static_cast<slick::orderbook::Side>(SIDE)>();
        if (!best_level || (order_price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(best_level->price, order_price))) {
            break;
        }
        assert(!best_level->orders.empty() && "Price level has no orders");

        const price_t level_price = best_level->price;
        allocate(*best_level, order_qty, book, check_self_match);
        ctx.legs_hint = allocations_.size() + 1;

        if (check_self_match) [[unlikely]] {
            // Pro-rata can allocate to any order on the level, so every own order on
            // it is a self-match, not just those the quantity reaches in queue order.
            bool cancelled = false;
            for (const auto &a : allocations_) {
                if (!isSelfMatch(order, a.order)) {
                    continue;
                }
                if (!handleSelfMatch(*this, order, a.order, book, request_time, event_time, seq_num, smp_mode)) {
                    // CANCEL_NEWEST backstop: prepareOrder should already have rejected this.
                    return std::make_tuple(OrdRejectReason::SMP, std::move(ctx.summaries));
                }
                cancelled = true;
            }
            if (cancelled) {
                continue;   // the level changed, and may be gone
            }
        }

        for (const auto &a : allocations_) {
            if (a.qty == 0) {
                continue;
            }
            order_qty -= a.qty;
            auto *full_book_order = check_self_match ? a.order : book.findOrder(a.order_id);
            if (!fillResting<SIDE>(ctx, order, a.order_id, full_book_order, level_price, a.qty, order_qty)) [[unlikely]] {
                abandoned = true;
                break;
            }
        }
    }

    cancelUnrestableRemainder(*this, order, order_qty, request_time);
    return std::make_tuple(OrdRejectReason::NONE, std::move(ctx.summaries));
}

template<Side SIDE>
std::vector<TradeSummaryInfo> ProRataMatchingEngine::matchFeed(uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, slick::sim::time_t event_time, uint64_t seq_num) {
    MatchContext ctx{*this, book, event_time, seq_num};

    while (qty > 0) {
        auto best_level = book.getBestLevel<static_cast<slick::orderbook::Side>(SIDE)>();
        if (!best_level || (price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(best_level->price, price))) {
            break;
        }
        assert(!best_level->orders.empty() && "Price level has no orders");

        const price_t level_price = best_level->price;
        allocate(*best_level, qty, book, false);
        ctx.legs_hint = allocations_.size() + 1;
        for (const auto &a : allocations_) {
            if (a.qty == 0) {
                continue;
            }
            if (!fillRestingFromFeed<SIDE>(ctx, order_id, a.order_id, level_price, a.qty, qty)) [[unlikely]] {
                return std::move(ctx.summaries);
            }
        }
    }

    return std::move(ctx.summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> ProRataMatchingEngine::match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, slick::sim::time_t request_time, slick::sim::time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    return order->side == Side::BUY
        ? matchOrder<Side::SELL>(order, order_price, order_qty, book, request_time, event_time, seq_num, smp_mode)
        : matchOrder<Side::BUY>(order, order_price, order_qty, book, request_time, event_time, seq_num, smp_mode);
}

std::vector<TradeSummaryInfo> ProRataMatchingEngine::match(Side side, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, slick::sim::time_t event_time, uint64_t seq_num) {
    return side == Side::BUY
        ? matchFeed<Side::SELL>(order_id, price, qty, book, event_time, seq_num)
        : matchFeed<Side::BUY>(order_id, price, qty, book, event_time, seq_num);
}
