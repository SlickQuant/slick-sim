#include "matching_engine.hpp"
#include "slick/logger.hpp"
#include <common/order.hpp>

namespace slick::sim::engine {

using Logger = slick::logger::Logger;

class FifoMatchingEngine : public MatchingEngine {
public:
    FifoMatchingEngine(
        slick::SlickQueue<Request> &request_queue,
        slick::SlickQueue<OrderResponse> &order_response_queue
    );
    ~FifoMatchingEngine() override = default;

    MatchingEngine::Type type() const noexcept override {
        return MatchingEngine::Type::FIFO;
    }

    std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> match(Order* order, OrderBook &book, uint64_t event_time, uint64_t seq_num=0) override;
    std::vector<TradeSummary> match(OrderBook &book, uint64_t event_time, uint64_t seq_num=0) override;

    // std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> addOrder(OrderBook &book, const AddOrderMessage &msg) override;
    // std::tuple<OrdRejectReason, Order*, std::vector<TradeSummary>> modifyOrder(OrderBook &book, const ModifyOrderMessage &msg) override;
    // std::tuple<OrdRejectReason, Order*> cancelOrder(OrderBook &book, const CancelOrderMessage &msg) override;

    // std::tuple<int, qty_t, uint32_t> onLevelUpdate(
    //     OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
    //     std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
    // ) const override;

protected:
    void match(OrderBook &book, Order *order, const std::vector<TradeSummary> &trades = {}) override;
};

    
} // namespace slick::sim
