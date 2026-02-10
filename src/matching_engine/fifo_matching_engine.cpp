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

// std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> FifoMatchingEngine::addOrder(OrderBook &book, const AddOrderMessage &msg) {
//     if (msg.client_order_id[0] != '\0') {
//         auto *order = book.getOrderByClientOrderId(msg.client_order_id);
//         if (order) {
//             LOG_WARN(std::format("ClientOrderId {} already exist", msg.client_order_id));
//         }
//     }
//     Order *order = book.allocateOrder();
//     order->client_order_id = msg.client_order_id;
//     std::vector<TradeSummary> trade_summaries;
//     match(book, order, trade_summaries);
//     return std::make_tuple(OrdRejectReason::NONE, order, trade_summaries);
// }

// std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> FifoMatchingEngine::modifyOrder(OrderBook &book, const ModifyOrderMessage &msg) {
//     std::vector<TradeSummary> trade_summaries;
//     return std::make_tuple(OrdRejectReason::NONE, nullptr, trade_summaries);
// }

// std::tuple<OrdRejectReason, Order*> FifoMatchingEngine::cancelOrder(OrderBook &book, const CancelOrderMessage &msg) {
//     return std::make_tuple(OrdRejectReason::NONE, nullptr);
// }

// std::tuple<int, qty_t, uint32_t> FifoMatchingEngine::onLevelUpdate(
//     OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
//     std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
// ) const {
//     return book.onLevelUpdate(side, price, qty, num_orders, level_updates, trade_updates);
// }

std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> FifoMatchingEngine::match(Order* order, OrderBook &book, uint64_t event_time, uint64_t seq_num)  {
    return std::make_tuple(OrdRejectReason::NONE, nullptr, std::vector<TradeSummary>{});
}

std::vector<TradeSummary> FifoMatchingEngine::match(OrderBook &book, uint64_t event_time, uint64_t seq_num) {
    auto *bid = book.getBestBid();
    auto *ask = book.getBestAsk();

    std::vector<TradeSummary> trades;

    while(bid->price >= ask->price) {

        auto it_bid_order = bid->orders.begin();
        auto it_ask_order = ask->orders.begin();

        auto total_level_trade_qty = std::min<qty_t>(bid->total_quantity, ask->total_quantity);
        auto accumulated_trad_qty = 0;

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