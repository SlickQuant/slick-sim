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

std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> FifoMatchingEngine::addOrder(OrderBook &book, const AddOrderMessage &msg) {
    if (msg.client_order_id[0] != '\0') {
        auto *order = book.getOrderByClientOrderId(msg.client_order_id);
        if (order) {
            LOG_WARN(std::format("ClientOrderId {} already exist", msg.client_order_id));
        }
    }
    Order *order = book.allocateOrder();
    order->client_order_id = msg.client_order_id;
    std::vector<TradeSummary> trade_summaries;
    match(book, order, trade_summaries);
    return std::make_tuple(OrdRejectReason::NONE, order, trade_summaries);
}

std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> FifoMatchingEngine::modifyOrder(OrderBook &book, const ModifyOrderMessage &msg) {
    std::vector<TradeSummary> trade_summaries;
    return std::make_tuple(OrdRejectReason::NONE, nullptr, trade_summaries);
}

std::tuple<OrdRejectReason, Order*> FifoMatchingEngine::cancelOrder(OrderBook &book, const CancelOrderMessage &msg) {
    return std::make_tuple(OrdRejectReason::NONE, nullptr);
}

std::tuple<int, qty_t, uint32_t> FifoMatchingEngine::onLevelUpdate(
    OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
    std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
) const {
    return book.onLevelUpdate(side, price, qty, num_orders, level_updates, trade_updates);
}

void FifoMatchingEngine::match(OrderBook &book, Order *order, const std::vector<TradeSummary> &trades) {
    // auto &side = book.getSide(order->side);
}