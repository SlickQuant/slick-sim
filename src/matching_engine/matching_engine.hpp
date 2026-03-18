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
        slick::SlickQueue<OrderResponse> &order_response_queue
    )
        : order_response_queue_(order_response_queue)
    {}

    virtual ~MatchingEngine() = default;

    virtual Type type() const noexcept = 0;

    static uint64_t nextTradeId() noexcept {
        return next_trade_id_.fetch_add(1, std::memory_order_relaxed);
    }

    virtual std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, uint64_t event_time, uint64_t seq_num = 0, SelfMatchPreventionMode smp_mode = SelfMatchPreventionMode::NONE) = 0;
    virtual std::vector<TradeSummaryInfo> match(OrderBook &book, uint64_t event_time, uint64_t seq_num = 0) = 0;

    virtual std::tuple<OrdRejectReason, std::vector<TradeSummary>> addOrder(OrderBook &book, Order *order) = 0;
    virtual std::tuple<OrdRejectReason, std::vector<TradeSummary>> modifyOrder(OrderBook &book, Order* order, price_t new_price, qty_t new_qty) = 0;
    // virtual std::tuple<OrdRejectReason, Order*> cancelOrder(OrderBook &book, const CancelOrderMessage &msg) = 0;

    // virtual std::tuple<int, qty_t, uint32_t> onLevelUpdate(
    //     OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
    //     std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
    // ) const = 0;

    void publishOrderAck(const Order *order);
    void publishOrderExecution(const Order *order);
    void publishOrderModify(const Order *order);
    void publishOrderCancel(const Order *order);

protected:
    virtual void match(OrderBook &book, Order *order, const std::vector<TradeSummary> &trades = {}) = 0;

protected:
    slick::SlickQueue<OrderResponse> &order_response_queue_;
    static std::atomic<uint64_t> next_trade_id_;
};

}   // end namespace slick::sim