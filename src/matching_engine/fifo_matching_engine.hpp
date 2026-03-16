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

    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, uint64_t event_time, uint64_t seq_num = 0) override;

    std::vector<TradeSummaryInfo> match(OrderBook &book, uint64_t event_time, uint64_t seq_num=0) override;

    std::tuple<OrdRejectReason, std::vector<TradeSummary>> addOrder(OrderBook &book, Order *order) override;
    std::tuple<OrdRejectReason, std::vector<TradeSummary>> modifyOrder(OrderBook &book, Order* order, price_t new_price, qty_t new_qty) override;
    // std::tuple<OrdRejectReason, Order*> cancelOrder(OrderBook &book, const CancelOrderMessage &msg) override;

    // std::tuple<int, qty_t, uint32_t> onLevelUpdate(
    //     OrderBook &book, Side side, price_t price, qty_t qty, uint32_t num_orders,
    //     std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
    // ) const override;

protected:
    void match(OrderBook &book, Order *order, const std::vector<TradeSummary> &trades = {}) override;
};

    
} // namespace slick::sim
