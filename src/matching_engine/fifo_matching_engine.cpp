#include "fifo_matching_engine.hpp"
#include "match_common.hpp"

using namespace slick::sim::engine;
using namespace slick::sim::engine::detail;
using namespace slick::sim;
using namespace slick::sim::utils;

FifoMatchingEngine::FifoMatchingEngine(
    slick::queue<OrderResponse> &response_queue
)
    : MatchingEngine(response_queue)
{
}

namespace {

template<Side SIDE>
std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> matchOrder(MatchingEngine& engine, Order* order, price_t order_price, qty_t order_qty, OrderBook &book, slick::sim::time_t request_time, slick::sim::time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    if (auto done = prepareOrder<SIDE, Reach::Queue>(engine, order, order_price, order_qty, book, request_time, smp_mode)) {
        return std::make_tuple(*done, std::vector<TradeSummaryInfo>{});
    }

    // Matching loop with SMP
    MatchContext ctx{engine, book, event_time, seq_num};
    order_qty = order->leaves_quantity;  // Use leaves quantity for matching

    while (order_qty > 0) {
        auto best_level = book.getBestLevel<static_cast<slick::orderbook::Side>(SIDE)>();

        // Check if we should continue matching at this level
        if (!best_level || (order_price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(best_level->price, order_price))) {
            break;
        }

        // Refresh orders reference each time (level may have been modified)
        auto& orders = best_level->orders;
        assert(!orders.empty() && "Price level has no orders");

        // FIFO: always the front of the queue
        auto &book_order = *orders.begin();

        // Check for a self-match
        auto* full_book_order = book.findOrder(book_order.order_id);
        if (smp_mode != SelfMatchPreventionMode::NONE && isSelfMatch(order, full_book_order)) [[unlikely]] {
            // Handle self-match according to Symbol's SMP mode
            bool should_continue = handleSelfMatch(engine, order, full_book_order, book,
                                                   request_time, event_time, seq_num, smp_mode);

            if (!should_continue) [[unlikely]] {
                // CANCEL_NEWEST backstop: prepareOrder should already have rejected this.
                // No cancel is published — the caller emits the one terminal REJECTED.
                return std::make_tuple(OrdRejectReason::SMP, std::move(ctx.summaries));
            }

            // CANCEL_RESTING mode: resting order was deleted, continue to next order
            continue;
        }

        qty_t trade_qty = std::min<qty_t>(order_qty, book_order.quantity);
        order_qty -= trade_qty;
        if (!fillResting<SIDE>(ctx, order, book_order.order_id, full_book_order, best_level->price, trade_qty, order_qty)) [[unlikely]] {
            break;
        }
    }

    cancelUnrestableRemainder(engine, order, order_qty, request_time);
    return std::make_tuple(OrdRejectReason::NONE, std::move(ctx.summaries));
}

template<Side SIDE>
std::vector<TradeSummaryInfo> matchFeed(MatchingEngine& engine, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, slick::sim::time_t event_time, uint64_t seq_num) {
    MatchContext ctx{engine, book, event_time, seq_num};

    while (qty > 0) {
        auto best_level = book.getBestLevel<static_cast<slick::orderbook::Side>(SIDE)>();

        // Check if we should continue matching at this level
        if (!best_level || (price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(best_level->price, price))) {
            break;
        }

        // Refresh orders reference each time (level may have been modified)
        auto& orders = best_level->orders;
        assert(!orders.empty() && "Price level has no orders");

        auto &book_order = *orders.begin();
        if (!fillRestingFromFeed<SIDE>(ctx, order_id, book_order.order_id, best_level->price,
                                       std::min<qty_t>(qty, book_order.quantity), qty)) [[unlikely]] {
            break;
        }
    }

    return std::move(ctx.summaries);
}

}   // namespace

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> FifoMatchingEngine::match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, slick::sim::time_t request_time, slick::sim::time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    return order->side == Side::BUY
        ? matchOrder<Side::SELL>(*this, order, order_price, order_qty, book, request_time, event_time, seq_num, smp_mode)
        : matchOrder<Side::BUY>(*this, order, order_price, order_qty, book, request_time, event_time, seq_num, smp_mode);
}

std::vector<TradeSummaryInfo> FifoMatchingEngine::match(Side side, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, slick::sim::time_t event_time, uint64_t seq_num) {
    return side == Side::BUY
        ? matchFeed<Side::SELL>(*this, order_id, price, qty, book, event_time, seq_num)
        : matchFeed<Side::BUY>(*this, order_id, price, qty, book, event_time, seq_num);
}
