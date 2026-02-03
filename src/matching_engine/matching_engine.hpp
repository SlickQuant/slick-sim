#pragma once

#include <memory>
#include <cstdint>
#include <tuple>
#include <order_book/order_book.hpp>
#include <common/messages.hpp>
#include <slick/queue.h>

namespace slick::sim::engine {

class MatchingEngine {
public:
    enum Type : uint8_t
    {
        FIFO,
        ProRata,
        __count__,  // for internal use only
    };

    MatchingEngine(
        slick::SlickQueue<Request> &request_queue,
        slick::SlickQueue<OrderResponse> &order_response_queue
    );
    virtual ~MatchingEngine() = default;

    virtual Type type() const noexcept = 0;
    virtual std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> addOrder(OrderBook &book, const AddOrderMessage &msg) = 0;
    virtual std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> modifyOrder(OrderBook &book, const ModifyOrderMessage &msg) = 0;
    virtual std::tuple<OrdRejectReason, Order*> cancelOrder(OrderBook &book, const CancelOrderMessage &msg) = 0;

    virtual std::tuple<int, qty_t, uint32_t> onLevelUpdate(
        OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
        std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
    ) const = 0;

protected:
    virtual void match(OrderBook &book, Order *order, const std::vector<TradeSummary> &trades = {}) = 0;

protected:
    slick::SlickQueue<Request> &request_queue_;
    slick::SlickQueue<OrderResponse> &order_response_queue_;
};


inline MatchingEngine::MatchingEngine(
    slick::SlickQueue<Request> &request_queue,
    slick::SlickQueue<OrderResponse> &order_response_queue
)
    : request_queue_(request_queue)
    , order_response_queue_(order_response_queue)
{
}

}   // end namespace slick::sim