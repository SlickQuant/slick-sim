#include "fifo_matching_engine.hpp"
#include <utils/price.hpp>

using namespace slick::sim::engine;
using namespace slick::sim;
using namespace slick::sim::utils;

FifoMatchingEngine::FifoMatchingEngine(
    slick::queue<OrderResponse> &response_queue
)
    : MatchingEngine(response_queue)
{
}

// Helper: Check if incoming order would self-match with book order
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

// Helper: Handle self-match according to Symbol's configured SMP mode
// Returns true if matching should continue, false if order should be rejected
inline bool handleSelfMatch(const Order* incoming_order, Order* book_order,
                            OrderBook& book, uint64_t event_time, uint64_t seq_num,
                            SelfMatchPreventionMode smp_mode) {
    switch (smp_mode) {
        case SelfMatchPreventionMode::NONE:
            // Allow self-matching
            return true;

        case SelfMatchPreventionMode::CANCEL_RESTING:
            // Cancel the resting book order and continue matching
            LOG_INFO("{} SMP: Canceling resting order {} (client_id={})",
                incoming_order->symbol, book_order->order_id, incoming_order->client_id);
            book.deleteOrder(book_order, event_time, seq_num, false);
            return true;  // Continue to next order

        case SelfMatchPreventionMode::CANCEL_NEWEST:
            // Reject the incoming order
            LOG_INFO("{} SMP: Rejecting new order {} due to self-match with resting order {} (client_id={})",
                incoming_order->symbol, incoming_order->id, book_order->order_id, incoming_order->client_id);
            return false;  // Signal to reject order

        default:
            return true;
    }
}

// Pre-check for Fill-Or-Kill: Determine if entire order can be filled
// Returns true if sufficient liquidity exists (considering SMP)
template<Side SIDE>
bool canFillCompletely(const Order* order, price_t order_price, qty_t order_qty, OrderBook& book,
                       SelfMatchPreventionMode smp_mode) {
    qty_t available_qty = 0;
    qty_t remaining_check_qty = order_qty;

    // Get all levels on the opposite side of the book
    const auto& levels = book.getLevelsL3(static_cast<slick::orderbook::Side>(SIDE));

    // Iterate through price levels in order (best to worst)
    for (const auto& [level_price, level_data] : levels) {
        // Check price compatibility - stop if price is worse than our limit
        if (order_price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(level_price, order_price)) {
            break;  // Reached price levels we can't match against
        }

        // Check each order in this level
        for (const auto& book_order : level_data.orders) {
            // Look up full order to check client_id
            auto* full_order = book.findOrder(book_order.order_id);

            // Handle self-matches based on SMP mode
            if (isSelfMatch(order, full_order)) {
                if (smp_mode == SelfMatchPreventionMode::CANCEL_NEWEST) {
                    // CANCEL_NEWEST: Order would be rejected on first self-match
                    return false;
                }
                else if (smp_mode == SelfMatchPreventionMode::CANCEL_RESTING) {
                    // CANCEL_RESTING: This resting order would be canceled, so skip it
                    LOG_INFO("{} FOK check: Skipping self-match with resting order {} due to SMP (CANCEL_RESTING mode)",
                        order->symbol, book_order.order_id);
                    continue;
                }
            }

            // Count available quantity
            qty_t usable_qty = std::min<qty_t>(book_order.quantity, remaining_check_qty);
            available_qty += usable_qty;
            remaining_check_qty -= usable_qty;

            if (remaining_check_qty == 0) {
                return true;  // Found enough liquidity
            }
        }

        // If we've filled the order completely, we're done
        if (remaining_check_qty == 0) {
            break;
        }
    }

    return available_qty >= order_qty;
}

template<Side SIDE>
std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(MatchingEngine& engine, Order* order, price_t order_price, qty_t order_qty, OrderBook &book, slick::sim::time_t request_time, slick::sim::time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    std::vector<TradeSummaryInfo> trade_summaries;

    if (order->status == OrderStatus::PENDING_NEW) {
        engine.publishOrderAck(order, request_time);
    }

    // STEP 1: Fill-Or-Kill pre-validation
    if (order->time_in_force == TimeInForce::FILL_OR_KILL) {
        if (!canFillCompletely<SIDE>(order, order_price, order_qty, book, smp_mode)) {
            engine.publishOrderCancel(order, request_time);   // Can't fill the order fully, immediate cancel
            LOG_INFO("{} FOK order {} cannot be completely filled, canceling", order->symbol, order->id);
            return std::make_tuple(OrdRejectReason::FOK_CANNOT_FILL, trade_summaries);
        }
    }

    if (order->status == OrderStatus::PENDING_REPLACE) {
        order->leaves_quantity += order_qty - order->quantity;  // Restore any reduced quantity from previous modify
        if (order->leaves_quantity < 0) {
            order->leaves_quantity = 0;  // Guard against negative leaves quantity
        }
        order->quantity = order_qty;
        order->price = order_price;
        engine.publishOrderModify(order, order_price, order_qty, request_time);
    }

    if (order->leaves_quantity == 0) {
        LOG_INFO("{} Order {} has zero leaves quantity after modification, canceling", order->symbol, order->id);
        engine.publishOrderCancel(order, request_time);
        return std::make_tuple(OrdRejectReason::NONE, trade_summaries);
    }

    // STEP 2: Matching loop with SMP
    auto last_fill_price = NULL_PRICE;
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

        auto order_it = orders.begin();
        auto &book_order = *order_it;

        // NEW: Self-Match Prevention check
        auto* full_book_order = book.findOrder(book_order.order_id);
        if (smp_mode != SelfMatchPreventionMode::NONE && isSelfMatch(order, full_book_order)) {
            // Handle self-match according to Symbol's SMP mode
            bool should_continue = handleSelfMatch(order, full_book_order, book, event_time, seq_num, smp_mode);

            if (!should_continue) {
                // CANCEL_NEWEST mode: reject incoming order
                engine.publishOrderCancel(order, request_time);
                LOG_INFO("{} Order {} rejected due to SMP (CANCEL_NEWEST mode)", order->symbol, order->id);
                return std::make_tuple(OrdRejectReason::SMP, trade_summaries);
            }

            // CANCEL_RESTING mode: resting order was deleted, continue to next order
            continue;
        }

        auto price = best_level->price;
        auto book_order_id = book_order.order_id;
        qty_t trade_qty = std::min<qty_t>(order_qty, book_order.quantity);

        LOG_INFO("{} Matching order {} with book order {} for qty {} at price {}", order->symbol, order->id, book_order_id, trade_qty, price);
        assert(trade_qty > 0);
        order->avg_fill_price = to_price_t(((to_price_double(order->avg_fill_price) * to_qty_double(order->cum_quantity)) + (to_price_double(price) * to_qty_double(trade_qty))) / to_price_double(order->cum_quantity + trade_qty));
        order->last_fill_price = price;
        order->last_fill_qty = trade_qty;
        order->cum_quantity += trade_qty;
        order->leaves_quantity -= trade_qty;
        assert(order->leaves_quantity >= 0);

        order_qty -= trade_qty;

        engine.publishOrderExecution(order);

        book.executeOrder(book_order_id, trade_qty, event_time, seq_num, order_qty == 0);

        if (last_fill_price != price) {
            // Create trade summary
            TradeSummaryInfo summary;
            summary.timestamp = event_time;
            summary.trade_id = MatchingEngine::nextTradeId();
            summary.aggressor_side = opposite_side<SIDE>();  // SIDE is the resting side, so aggressor is the opposite
            summary.price = price;
            summary.qty = trade_qty;
            summary.num_orders = 2;
            summary.Trades.emplace_back(Trade{order->id, trade_qty});
            summary.Trades.emplace_back(Trade{book_order_id, trade_qty});

            trade_summaries.emplace_back(std::move(summary));
            last_fill_price = price;
        } else {
            // Update last trade summary for this price level
            auto &last_summary = trade_summaries.back();
            last_summary.num_orders += 1;
            last_summary.qty += trade_qty;
            last_summary.Trades.front().qty += trade_qty; // Update aggressor trade qty
            last_summary.Trades.emplace_back(Trade{book_order_id, trade_qty});
        }
    }

    if (order->time_in_force == TimeInForce::IMMEDIATE_OR_CANCEL && order_qty > 0) {
        // FOK: Cancel any remaining quantity if we couldn't fill the entire order
        engine.publishOrderCancel(order, request_time);
        LOG_INFO("{} Order {} has TIF=IOC and was not completely filled, canceling remaining qty {}", order->symbol, order->id, order_qty);
    }

    return std::make_tuple(OrdRejectReason::NONE, trade_summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> FifoMatchingEngine::match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, slick::sim::time_t request_time, slick::sim::time_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    return order->side == Side::BUY
        ? ::match<Side::SELL>(*this, order, order_price, order_qty, book, request_time, event_time, seq_num, smp_mode)
        : ::match<Side::BUY>(*this, order, order_price, order_qty, book, request_time, event_time, seq_num, smp_mode);
}

template<Side SIDE>
std::vector<TradeSummaryInfo> match(MatchingEngine& engine, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, slick::sim::time_t event_time, uint64_t seq_num) {
    std::vector<TradeSummaryInfo> trade_summaries;

    auto last_fill_price = NULL_PRICE;

    while (qty > 0) {
        auto best_level = book.getBestLevel<static_cast<slick::orderbook::Side>(SIDE)>();

        // Check if we should continue matching at this level
        if (!best_level || (price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(best_level->price, price))) {
            break;
        }

        // Refresh orders reference each time (level may have been modified)
        auto& orders = best_level->orders;
        assert(!orders.empty() && "Price level has no orders");

        auto order_it = orders.begin();
        auto &book_order = *order_it;

        auto trade_price = best_level->price;
        auto book_order_id = book_order.order_id;
        qty_t trad_qty = std::min<qty_t>(qty, book_order.quantity);
        
        LOG_TRACE("  Matching with {} book order {} for qty {} at price {}", order_id, book_order_id, trad_qty, trade_price);

        auto* our_order = book.findOrder(book_order_id);
        book.executeOrder(book_order_id, trad_qty, event_time, seq_num, qty == 0);

        if (our_order) {
            // our_order->avg_fill_price = ((our_order->avg_fill_price * our_order->cum_quantity) + (trade_price * trad_qty)) / (our_order->cum_quantity + trad_qty);
            // our_order->last_fill_price = trade_price;
            // our_order->last_fill_qty = trad_qty;
            // our_order->cum_quantity += trad_qty;
            // our_order->leaves_quantity -= trad_qty;
            engine.publishOrderExecution(our_order);
            LOG_INFO("{} order {} filled {}@{}, leaves_qty={}, executed_qty={}, avg_fill_price={}", our_order->symbol, our_order->order_id, to_qty_double(trad_qty), to_price_double(trade_price), to_qty_double(our_order->leaves_quantity), to_qty_double(our_order->cum_quantity), to_price_double(our_order->avg_fill_price));
            if (our_order->leaves_quantity == 0) {
                book.deleteOrder(our_order);
            }
        }

        qty -= trad_qty;

        if (last_fill_price != trade_price) {
            // Create trade summary
            TradeSummaryInfo summary;
            summary.timestamp = event_time;
            summary.trade_id = MatchingEngine::nextTradeId();
            summary.aggressor_side = opposite_side<SIDE>();  // SIDE is the resting side, so aggressor is the opposite
            summary.price = trade_price;
            summary.qty = trad_qty;
            summary.num_orders = 2;
            summary.Trades.emplace_back(Trade{order_id, trad_qty});
            summary.Trades.emplace_back(Trade{book_order_id, trad_qty});

            trade_summaries.emplace_back(std::move(summary));
            last_fill_price = trade_price;
        } else {
            // Update last trade summary for this price level
            auto &last_summary = trade_summaries.back();
            last_summary.num_orders += 1;
            last_summary.qty += trad_qty;
            last_summary.Trades.front().qty += trad_qty; // Update aggressor trade qty
            last_summary.Trades.emplace_back(Trade{book_order_id, trad_qty});
        }
    }

    return trade_summaries;
}

std::vector<TradeSummaryInfo> FifoMatchingEngine::match(Side side, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, slick::sim::time_t event_time, uint64_t seq_num) {
    return side == Side::BUY
        ? ::match<Side::SELL>(*this, order_id, price, qty, book, event_time, seq_num)
        : ::match<Side::BUY>(*this, order_id, price, qty, book, event_time, seq_num);
}