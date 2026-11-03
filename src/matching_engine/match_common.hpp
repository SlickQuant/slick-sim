#pragma once

#include "matching_engine.hpp"
#include <slick/logger.hpp>
#include <common/order.hpp>
#include <utils/order.hpp>
#include <utils/price.hpp>
#include <algorithm>
#include <cassert>
#include <optional>
#include <vector>

// Building blocks both engines share. They differ only in which resting orders an
// aggressor trades with at a price level; the preamble before matching, a single
// fill, and the trade summaries a match produces are the same.
namespace slick::sim::engine::detail {

/// How far into a price level an aggressor reaches. FIFO trades the queue front
/// to back until its quantity runs out. Pro-rata allocates across the whole level,
/// so every order on a level it reaches may trade.
enum class Reach : uint8_t { Queue, Level };

// Check if incoming order would self-match with book order
// Returns true if orders are from same client
inline bool isSelfMatch(const Order* incoming_order, const Order* book_order) {
    if (book_order == nullptr) {
        return false;  // Book order not found, assume different client
    }

    // SMP only applies when both orders have valid client_ids
    if (incoming_order->client_id == -1 || book_order->client_id == -1) {
        return false;
    }

    return incoming_order->client_id == book_order->client_id;
}

// Handle self-match according to Symbol's configured SMP mode
// Returns true if matching should continue, false if order should be rejected
inline bool handleSelfMatch(MatchingEngine& engine, const Order* incoming_order, Order* book_order,
                            OrderBook& book, slick::sim::time_t request_time,
                            uint64_t event_time, uint64_t seq_num,
                            SelfMatchPreventionMode smp_mode) {
    switch (smp_mode) {
        case SelfMatchPreventionMode::NONE:
            // Allow self-matching
            return true;

        case SelfMatchPreventionMode::CANCEL_RESTING:
            // Cancel the resting book order and continue matching. This is a real
            // cancel, so it takes the same three steps as Symbol::cancelOrder:
            // remove it from the book, tell its owner, and return it to the pool.
            // Deleting it from the book alone would leave the client believing the
            // order was still live and leak the pooled Order.
            LOG_INFO("{} SMP: Canceling resting order {} (client_id={})",
                incoming_order->symbol.view(), book_order->order_id.view(), incoming_order->client_id);
            book.deleteOrder(book_order, event_time, seq_num, false);
            engine.publishOrderCancel(book_order, request_time);
            book.freeOrder(book_order);
            return true;  // Continue to next order

        case SelfMatchPreventionMode::CANCEL_NEWEST:
            // Reject the incoming order. Reached only as a backstop — the
            // pre-check in prepareOrder() decides this before anything is mutated.
            LOG_INFO("{} SMP: Rejecting new order {} due to self-match with resting order {} (client_id={})",
                incoming_order->symbol.view(), incoming_order->id, book_order->order_id.view(), incoming_order->client_id);
            return false;  // Signal to reject order

        default:
            return true;
    }
}

// Pre-check for CANCEL_NEWEST: would this order reach a resting order of its own?
//
// This has to be answered before the order is touched. An amendment mutates
// price/quantity and reports REPLACED on its way into the matching loop, and a
// new order is acked there, so discovering the self-match mid-loop would mean
// telling the client the order was replaced or live and then rejecting it.
// Only orders the incoming quantity could actually reach are considered.
template<Side SIDE, Reach REACH>
bool wouldSelfMatch(const Order* order, price_t order_price, qty_t order_qty, OrderBook& book) {
    qty_t remaining = order_qty;

    const auto& levels = book.getLevelsL3(static_cast<slick::orderbook::Side>(SIDE));
    for (const auto& [level_price, level_data] : levels) {
        if (remaining <= 0) [[unlikely]] {
            return false;
        }
        if (order_price != NULL_PRICE && !utils::isPriceBetterOrEqual<SIDE>(level_price, order_price)) [[unlikely]] {
            break;  // Reached price levels we can't match against
        }

        for (const auto& book_order : level_data.orders) {
            if constexpr (REACH == Reach::Queue) {
                if (remaining <= 0) [[unlikely]] {
                    return false;
                }
            }
            if (isSelfMatch(order, book.findOrder(book_order.order_id))) [[unlikely]] {
                return true;
            }
            if constexpr (REACH == Reach::Queue) {
                remaining -= std::min<qty_t>(book_order.quantity, remaining);
            }
        }

        if constexpr (REACH == Reach::Level) {
            remaining -= std::min<qty_t>(level_data.total_quantity, remaining);
        }
    }
    return false;
}

// Pre-check for Fill-Or-Kill: Determine if entire order can be filled
// Returns true if sufficient liquidity exists (considering SMP). Total crossing
// liquidity is the same whichever way it is allocated, so this serves both engines.
template<Side SIDE>
bool canFillCompletely(const Order* order, price_t order_price, qty_t order_qty, OrderBook& book,
                       SelfMatchPreventionMode smp_mode) {
    qty_t remaining_check_qty = order_qty;

    // Self-match handling is the only reason to resolve a book order to its full
    // Order, and that is a hash lookup per resting order. With SMP off nothing
    // downstream needs it, so the whole scan stays inside the level's own array.
    const bool check_self_match = smp_mode != SelfMatchPreventionMode::NONE;

    // Get all levels on the opposite side of the book
    const auto& levels = book.getLevelsL3(static_cast<slick::orderbook::Side>(SIDE));

    // Iterate through price levels in order (best to worst)
    for (const auto& [level_price, level_data] : levels) {
        // Check price compatibility - stop if price is worse than our limit
        if (order_price != NULL_PRICE && !utils::isPriceBetterOrEqual<SIDE>(level_price, order_price)) [[unlikely]] {
            break;  // Reached price levels we can't match against
        }

        // Check each order in this level
        for (const auto& book_order : level_data.orders) {
            // Handle self-matches based on SMP mode (exceptional case)
            if (check_self_match && isSelfMatch(order, book.findOrder(book_order.order_id))) [[unlikely]] {
                if (smp_mode == SelfMatchPreventionMode::CANCEL_NEWEST) [[unlikely]] {
                    // CANCEL_NEWEST: Order would be rejected on first self-match
                    return false;
                }
                // CANCEL_RESTING: This resting order would be canceled, so skip it.
                // NONE never gets here - the quantity stays countable because the
                // order really would trade against itself.
                LOG_INFO("{} FOK check: Skipping self-match with resting order {} due to SMP (CANCEL_RESTING mode)",
                    order->symbol.view(), book_order.order_id);
                continue;
            }

            // Count available quantity (normal path)
            remaining_check_qty -= std::min<qty_t>(book_order.quantity, remaining_check_qty);

            if (remaining_check_qty == 0) [[likely]] {
                return true;  // Found enough liquidity
            }
        }
    }

    // Every path that finds enough liquidity has already returned, so reaching
    // here means the book ran out - except for a zero-quantity order, which is
    // trivially fillable.
    return remaining_check_qty == 0;
}

/// Everything a client order goes through before it matches: the CANCEL_NEWEST
/// pre-check, the ack, the FOK pre-check, and applying a pending replace.
/// Returns the reject reason when the order is finished here, or nullopt when it
/// goes on to match its `leaves_quantity`.
template<Side SIDE, Reach REACH>
std::optional<OrdRejectReason> prepareOrder(MatchingEngine& engine, Order* order, price_t order_price, qty_t order_qty,
                                            OrderBook& book, slick::sim::time_t request_time, SelfMatchPreventionMode smp_mode) {
    // STEP 0: CANCEL_NEWEST is decided before the order is acked, mutated or
    // reported on, so a rejected order leaves no trace: no NEW, no REPLACED, no
    // partial fills, and the stored Order keeps its original price and quantity.
    // The caller publishes the single terminal REJECTED.
    if (smp_mode == SelfMatchPreventionMode::CANCEL_NEWEST) {
        // Scan only as deep as this order will really trade. `order_qty` is the
        // order's *total* quantity, which for a partially filled amendment is
        // more than its remaining leaves — scanning that far could reach one of
        // the owner's resting orders the amendment would never touch and reject
        // a perfectly good fill. Mirror the leaves the matching loop is about to
        // compute below, without mutating anything yet.
        qty_t effective_qty = order->leaves_quantity;
        if (order->status == OrderStatus::PENDING_REPLACE) {
            effective_qty += order_qty - order->quantity;
            if (effective_qty < 0) {
                effective_qty = 0;
            }
        }

        if (effective_qty > 0 && wouldSelfMatch<SIDE, REACH>(order, order_price, effective_qty, book)) {
            LOG_INFO("{} Order {} rejected due to SMP (CANCEL_NEWEST mode)", order->symbol.view(), order->id);
            return OrdRejectReason::SMP;
        }
    }

    if (order->status == OrderStatus::PENDING_NEW) {
        engine.publishOrderAck(order, request_time);
    }

    // STEP 1: Fill-Or-Kill pre-validation
    if (order->time_in_force == TimeInForce::FILL_OR_KILL) [[unlikely]] {
        if (!canFillCompletely<SIDE>(order, order_price, order_qty, book, smp_mode)) [[unlikely]] {
            engine.publishOrderCancel(order, request_time);   // Can't fill the order fully, immediate cancel
            LOG_INFO("{} FOK order {} cannot be completely filled, canceling", order->symbol.view(), order->id);
            return OrdRejectReason::FOK_CANNOT_FILL;
        }
    }

    // STEP 2: apply a pending replace
    if (order->status == OrderStatus::PENDING_REPLACE) [[unlikely]] {
        order->quantity = order_qty;
        order->price = order_price;
        if (order->cum_quantity >= order_qty) [[unlikely]] {
            order->leaves_quantity = 0;
        } else {
            order->leaves_quantity = order_qty - order->cum_quantity;
        }
        engine.publishOrderModify(order, order_price, order_qty, request_time);
    }

    if (order->leaves_quantity == 0) [[unlikely]] {
        LOG_INFO("{} Order {} has zero leaves quantity after modification, canceling", order->symbol.view(), order->id);
        engine.publishOrderCancel(order, request_time);
        return OrdRejectReason::NONE;
    }
    return std::nullopt;
}

/// State one match() call carries through its fills.
struct MatchContext {
    MatchingEngine& engine;
    OrderBook& book;
    slick::sim::time_t event_time;
    uint64_t seq_num;
    std::vector<TradeSummaryInfo> summaries{};
    price_t last_fill_price = NULL_PRICE;
    /// Legs a summary started at the current level may reach, aggressor included.
    /// Pro-rata fills every order on a level at once, so it sets this to size the
    /// summary's Trades once; FIFO leaves it 0.
    size_t legs_hint = 0;
};

/// Adds one fill to the trade summaries, which are aggregated per price level:
/// a new summary when the price changes, otherwise one more leg on the current
/// one. Trades[0] is the aggressor's running total and Trades[1..] the resting
/// orders it hit; num_orders grows with Trades, as publishTradeSummary needs.
template<Side SIDE>
void recordTrade(MatchContext& ctx, uint64_t aggressor_id, uint64_t book_order_id, price_t price,
                 qty_t trade_qty, qty_t phantom_qty) {
    if (ctx.last_fill_price != price) [[unlikely]] {
        TradeSummaryInfo summary;
        summary.timestamp = ctx.event_time;
        summary.trade_id = MatchingEngine::nextTradeId();
        summary.aggressor_side = opposite_side<SIDE>();  // SIDE is the resting side, so aggressor is the opposite
        summary.price = price;
        summary.qty = trade_qty;
        summary.phantom_qty = phantom_qty;
        summary.num_orders = 2;
        if (ctx.legs_hint > summary.Trades.capacity()) {
            summary.Trades.reserve(ctx.legs_hint);
        }
        summary.Trades.emplace_back(Trade{aggressor_id, trade_qty});
        summary.Trades.emplace_back(Trade{book_order_id, trade_qty});

        ctx.summaries.emplace_back(std::move(summary));
        ctx.last_fill_price = price;
    } else {
        auto &last_summary = ctx.summaries.back();
        last_summary.num_orders += 1;
        last_summary.qty += trade_qty;
        last_summary.phantom_qty += phantom_qty;
        last_summary.Trades.front().qty += trade_qty; // Update aggressor trade qty
        last_summary.Trades.emplace_back(Trade{book_order_id, trade_qty});
    }
}

/// One fill of a client order against a resting order. `remaining` is the
/// aggressor's quantity after this fill, so reaching 0 closes the book's batch.
/// `full_book_order` is the resting order's Order, or nullptr for phantom liquidity.
/// Returns false when the book declines the execution: the aggressor has already
/// been filled and reported by then, so the caller stops rather than unwinding.
template<Side SIDE>
bool fillResting(MatchContext& ctx, Order* order, uint64_t book_order_id, Order* full_book_order,
                 price_t price, qty_t trade_qty, qty_t remaining) {
    LOG_INFO("{} Matching order {} with book order {} for qty {} at price {}", order->symbol.view(), order->id, book_order_id, trade_qty, price);
    utils::executeOrder(order, price, trade_qty, ctx.event_time);
    ctx.engine.publishOrderExecution(order, true);

    if (!ctx.book.executeOrder(book_order_id, trade_qty, ctx.event_time, ctx.seq_num, remaining == 0)) [[unlikely]] {
        // Same hazard as the market-data replay path: a declined execution leaves
        // the resting order intact, so looping would re-match it. The aggressor has
        // already been filled and reported by this point, so there is no unwinding
        // it here - stop and leave the discrepancy loud rather than compounding it.
        LOG_ERROR("{} book rejected execution of order {} for qty {} at price {} (seq_num={}); abandoning match",
            order->symbol.view(), book_order_id, trade_qty, price, ctx.seq_num);
        return false;
    }

    // A trade has two sides, and when the resting side is one of the
    // simulator's own orders it belongs to a client who has to hear about it.
    // book.executeOrder() above booked the fill onto it, but OrderBook holds no
    // reference to the engine so it cannot publish, and on a complete fill it
    // only drops the order from the lookup maps - leaving the pooled Order
    // unreturned. Both are done here, exactly as the market-data replay path
    // does them.
    if (full_book_order) [[unlikely]] {
        ctx.engine.publishOrderExecution(full_book_order, false);
        if (full_book_order->leaves_quantity == 0) {
            // Erases whatever the maps still hold and returns it to the pool.
            ctx.book.deleteOrder(full_book_order);
        }
    }

    // A resting order the simulator does not own is phantom liquidity mirroring
    // the real market. Tracked separately so the exchange can tell how much of
    // this trade the venue's own level update will also account for.
    recordTrade<SIDE>(ctx, order->id, book_order_id, price, trade_qty, full_book_order ? qty_t{0} : trade_qty);
    return true;
}

/// One fill of market-data liquidity against a resting order. There is no
/// aggressor Order to fill or report. `qty` is the aggressor's remaining quantity:
/// it is decremented by the fill, and restored when the book declines it, in which
/// case this returns false and the caller stops.
template<Side SIDE>
bool fillRestingFromFeed(MatchContext& ctx, uint64_t aggressor_id, uint64_t book_order_id, price_t price,
                         qty_t trade_qty, qty_t& qty) {
    LOG_TRACE("  Matching with {} book order {} for qty {} at price {}", aggressor_id, book_order_id, trade_qty, price);

    auto* our_order = ctx.book.findOrder(book_order_id);

    // Decrement before booking the fill so qty == 0 marks the final fill.
    // Checking first would always be false while the caller's loop runs, leaving
    // F_END_EVENT unset and clients waiting for the batch to close.
    qty -= trade_qty;
    if (!ctx.book.executeOrder(book_order_id, trade_qty, ctx.event_time, ctx.seq_num, qty == 0)) [[unlikely]] {
        // The execution failed, so the resting quantity is unchanged. Restore the
        // aggressor quantity and stop; retrying would repeatedly report fills
        // against the same liquidity.
        qty += trade_qty;
        LOG_ERROR("{} book rejected execution of order {} for qty {} at price {} (seq_num={}); abandoning match with {} unfilled",
            ctx.book.symbolName().view(), book_order_id, trade_qty, price, ctx.seq_num, qty);
        return false;
    }

    if (our_order) {
        ctx.engine.publishOrderExecution(our_order, false);
        LOG_INFO("{} order {} filled {}@{}, leaves_qty={}, executed_qty={}, avg_fill_price={}", our_order->symbol.view(), our_order->order_id.view(), to_qty_double(trade_qty), to_price_double(price), to_qty_double(our_order->leaves_quantity), to_qty_double(our_order->cum_quantity), to_price_double(our_order->avg_fill_price));
        if (our_order->leaves_quantity == 0) {
            ctx.book.deleteOrder(our_order);
        }
    }

    recordTrade<SIDE>(ctx, aggressor_id, book_order_id, price, trade_qty, our_order ? qty_t{0} : trade_qty);
    return true;
}

/// What cannot rest is cancelled: an IOC's remainder, and a market order's, which
/// would otherwise rest at NULL_PRICE.
inline void cancelUnrestableRemainder(MatchingEngine& engine, const Order* order, qty_t remaining,
                                      slick::sim::time_t request_time) {
    if (remaining > 0 && !order->restsRemainder()) {
        engine.publishOrderCancel(order, request_time);
        LOG_INFO("{} Order {} cannot rest and was not completely filled, canceling remaining qty {}", order->symbol.view(), order->id, remaining);
    }
}

}   // namespace slick::sim::engine::detail
