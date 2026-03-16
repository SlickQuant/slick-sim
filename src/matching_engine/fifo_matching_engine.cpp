#include "fifo_matching_engine.hpp"

using namespace slick::sim::engine;
using namespace slick::sim;

FifoMatchingEngine::FifoMatchingEngine(
    slick::SlickQueue<Request> &request_queue,
    slick::SlickQueue<OrderResponse> &order_response_queue
)
    : MatchingEngine(request_queue, order_response_queue)
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

template<Side SIDE>
bool isPriceBetterOrEqual(price_t price1, price_t price2) {
    if constexpr (SIDE == Side::BUY) {
        return price1 >= price2;
    }
    else {
        return price1 <= price2;
    }
}

template<Side SIDE>
std::tuple<OrdRejectReason, std::vector<MatchingEngine::TradeSummaryInfo>> match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, uint64_t event_time, uint64_t seq_num) {
    std::vector<MatchingEngine::TradeSummaryInfo> trade_summaries;
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
        qty_t trad_qty = std::min<qty_t>(order_qty, book_order.quantity);

        LOG_INFO("{} Matching order {} with book order {} for qty {} at price {}", order->symbol, order->id, book_order.order_id, trad_qty, book_order.price);

        order->last_filled_price = book_order.price;
        order->last_filled_qty = trad_qty;
        order->filled_quantity += trad_qty;
        order->leaves_quantity -= trad_qty;

        order_qty -= trad_qty;

        if (last_fill_price != best_level->price) {
            // Create trade summary
            MatchingEngine::TradeSummaryInfo summary;
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

std::tuple<OrdRejectReason, std::vector<MatchingEngine::TradeSummaryInfo>> FifoMatchingEngine::match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, uint64_t event_time, uint64_t seq_num) {
    return order->side == Side::BUY
        ? ::match<Side::SELL>(order, order_price, order_qty, book, event_time, seq_num)
        : ::match<Side::BUY>(order, order_price, order_qty, book, event_time, seq_num); 
}

std::vector<MatchingEngine::TradeSummaryInfo> FifoMatchingEngine::match(OrderBook &book, uint64_t event_time, uint64_t seq_num) {
    auto *bid = book.getBestBid();
    auto *ask = book.getBestAsk();

    std::vector<MatchingEngine::TradeSummaryInfo> trades;

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