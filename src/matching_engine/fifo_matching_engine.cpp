#include "fifo_matching_engine.hpp"

using namespace slick::sim::engine;
using namespace slick::sim;

FifoMatchingEngine::FifoMatchingEngine(
    slick::SlickQueue<OrderResponse> &order_response_queue
)
    : MatchingEngine(order_response_queue)
{
}

std::tuple<OrdRejectReason, std::vector<TradeSummary>> FifoMatchingEngine::addOrder(OrderBook &book, Order *order) {
    std::vector<TradeSummary> trade_summaries;
    match(book, order, trade_summaries);
    return std::make_tuple(OrdRejectReason::NONE, trade_summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummary>> FifoMatchingEngine::modifyOrder(OrderBook &book, Order* order, price_t new_price, qty_t new_qty) {
    std::vector<TradeSummary> trade_summaries;
    return std::make_tuple(OrdRejectReason::NONE, trade_summaries);
}

// std::tuple<OrdRejectReason, Order*> FifoMatchingEngine::cancelOrder(OrderBook &book, const CancelOrderMessage &msg) {
//     return std::make_tuple(OrdRejectReason::NONE, nullptr);
// }

// std::tuple<int, qty_t, uint32_t> FifoMatchingEngine::onLevelUpdate(
//     OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
//     std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
// ) const {
//     return book.onLevelUpdate(side, price, qty, num_orders, level_updates, trade_updates);
// }

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
inline bool handleSelfMatch(const Order* incoming_order, const slick::orderbook::detail::Order& book_order,
                            OrderBook& book, uint64_t event_time, uint64_t seq_num,
                            SelfMatchPreventionMode smp_mode) {
    switch (smp_mode) {
        case SelfMatchPreventionMode::NONE:
            // Allow self-matching
            return true;

        case SelfMatchPreventionMode::CANCEL_RESTING:
            // Cancel the resting book order and continue matching
            LOG_INFO("{} SMP: Canceling resting order {} (client_id={})",
                incoming_order->symbol, book_order.order_id, incoming_order->client_id);
            book.deleteOrder(book_order.order_id, event_time, seq_num, false);
            return true;  // Continue to next order

        case SelfMatchPreventionMode::CANCEL_NEWEST:
            // Reject the incoming order
            LOG_INFO("{} SMP: Rejecting new order {} due to self-match with resting order {} (client_id={})",
                incoming_order->symbol, incoming_order->id, book_order.order_id, incoming_order->client_id);
            return false;  // Signal to reject order

        default:
            return true;
    }
}

template<Side SIDE>
bool isPriceBetterOrEqual(price_t price1, price_t price2) {
    if constexpr (SIDE == Side::BUY) {
        return price1 >= price2;
    }
    else {
        return price1 <= price2;
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
        if (order_price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(order_price, level_price)) {
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
                // CANCEL_RESTING or NONE: Skip this order (will be handled during matching)
                continue;
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
std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(MatchingEngine& engine, Order* order, price_t order_price, qty_t order_qty, OrderBook &book, uint64_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    std::vector<TradeSummaryInfo> trade_summaries;

    engine.publishOrderAck(order);

    // STEP 1: Fill-Or-Kill pre-validation
    if (order->time_in_force == TimeInForce::FILL_OR_KILL) {
        if (!canFillCompletely<SIDE>(order, order_price, order_qty, book, smp_mode)) {
            engine.publishOrderCancel(order);   // Can't fill the order fully, immediate cancel
            LOG_INFO("{} FOK order {} cannot be completely filled, canceling", order->symbol, order->id);
            return std::make_tuple(OrdRejectReason::NONE, trade_summaries);
        }
    }

    // STEP 2: Matching loop with SMP
    qty_t initial_order_qty = order_qty;
    auto last_fill_price = NULL_PRICE;

    while (order_qty > 0) {
        auto best_level = book.getBestLevel<static_cast<slick::orderbook::Side>(SIDE)>();

        // Check if we should continue matching at this level
        if (!best_level || (order_price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(order_price, best_level->price))) {
            break;
        }

        // Refresh orders reference each time (level may have been modified)
        auto& orders = best_level->orders;
        assert(!orders.empty() && "Price level has no orders");

        auto order_it = orders.begin();
        auto &book_order = *order_it;

        // NEW: Self-Match Prevention check
        auto* full_book_order = book.findOrder(book_order.order_id);
        if (isSelfMatch(order, full_book_order)) {
            // Handle self-match according to Symbol's SMP mode
            bool should_continue = handleSelfMatch(order, book_order, book, event_time, seq_num, smp_mode);

            if (!should_continue) {
                // CANCEL_NEWEST mode: reject incoming order
                engine.publishOrderCancel(order);
                LOG_INFO("{} Order {} rejected due to SMP (CANCEL_NEWEST mode)", order->symbol, order->id);
                return std::make_tuple(OrdRejectReason::SMP, trade_summaries);
            }

            // CANCEL_RESTING mode: resting order was deleted, continue to next order
            // NONE mode: not reached (isSelfMatch returned false)
            continue;
        }

        qty_t trad_qty = std::min<qty_t>(order_qty, book_order.quantity);

        LOG_INFO("{} Matching order {} with book order {} for qty {} at price {}", order->symbol, order->id, book_order.order_id, trad_qty, book_order.price);

        order->last_filled_price = book_order.price;
        order->last_filled_qty = trad_qty;
        order->filled_quantity += trad_qty;
        order->leaves_quantity -= trad_qty;

        order_qty -= trad_qty;

        engine.publishOrderExecution(order);

        if (last_fill_price != best_level->price) {
            // Create trade summary
            TradeSummaryInfo summary;
            summary.timestamp = event_time;
            summary.trade_id = MatchingEngine::nextTradeId();
            summary.aggressor_side = SIDE;
            summary.price = best_level->price;
            summary.qty = trad_qty;
            summary.num_orders = 2;
            summary.Trades.emplace_back(Trade{order->id, trad_qty});
            summary.Trades.emplace_back(Trade{book_order.order_id, trad_qty});

            trade_summaries.emplace_back(std::move(summary));
            last_fill_price = best_level->price;
        } else {
            // Update last trade summary for this price level
            auto &last_summary = trade_summaries.back();
            last_summary.num_orders += 1;
            last_summary.qty += trad_qty;
            last_summary.Trades.front().qty += trad_qty; // Update aggressor trade qty
            last_summary.Trades.emplace_back(Trade{book_order.order_id, trad_qty});
        }

        book.executeOrder(book_order.order_id, trad_qty, seq_num, order_qty == 0);
    }

    return std::make_tuple(OrdRejectReason::NONE, trade_summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> FifoMatchingEngine::match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, uint64_t event_time, uint64_t seq_num, SelfMatchPreventionMode smp_mode) {
    return order->side == Side::BUY
        ? ::match<Side::SELL>(*this, order, order_price, order_qty, book, event_time, seq_num, smp_mode)
        : ::match<Side::BUY>(*this, order, order_price, order_qty, book, event_time, seq_num, smp_mode);
}

std::vector<TradeSummaryInfo> FifoMatchingEngine::match(OrderBook &book, uint64_t event_time, uint64_t seq_num) {
    auto *bid = book.getBestBid();
    auto *ask = book.getBestAsk();

    std::vector<TradeSummaryInfo> trades;

    while(bid->price >= ask->price) {

        auto it_bid_order = bid->orders.begin();
        auto it_ask_order = ask->orders.begin();

        qty_t total_level_trade_qty = std::min<qty_t>(bid->total_quantity, ask->total_quantity);
        qty_t accumulated_trad_qty = 0;

        while (it_bid_order != bid->orders.end() && it_ask_order != ask->orders.end()) {
            auto traded_qty = std::min<qty_t>(it_bid_order->quantity, it_ask_order->quantity);
            accumulated_trad_qty += traded_qty;

            auto *bid_order = book.findOrder(it_bid_order->order_id);
            auto *ask_order = book.findOrder(it_ask_order->order_id);

            bool is_end_event = bid->price == ask->price && total_level_trade_qty == accumulated_trad_qty;
    
            if (bid_order) {
                SLICK_ASSERT(bid_order->quantity == it_bid_order->quantity);
                book.modifyOrder(bid_order, bid_order->price, bid_order->quantity - traded_qty,
                    event_time, seq_num);
            }
            else {
                book.modifyOrder(it_bid_order->order_id, it_bid_order->price, it_bid_order->quantity - traded_qty, event_time,
                    it_bid_order->priority, seq_num);
            }
    
            if (ask_order) {
                SLICK_ASSERT(ask_order->quantity == it_ask_order->quantity);
                book.modifyOrder(ask_order, ask_order->price, bid_order->quantity - traded_qty,
                    event_time, seq_num, is_end_event);
            }
            else {
                book.modifyOrder(it_ask_order->order_id, it_ask_order->price, it_ask_order->quantity - traded_qty, event_time,
                    it_ask_order->priority, seq_num, is_end_event);
            }

            if (accumulated_trad_qty == total_level_trade_qty) {
                break;
            }

            it_bid_order = bid->orders.begin();
            it_ask_order = ask->orders.begin();
        }
    }
    return trades;
}

void FifoMatchingEngine::match(OrderBook &book, Order *order, const std::vector<TradeSummary> &trades) {
    // auto &side = book.getSide(order->side);
}