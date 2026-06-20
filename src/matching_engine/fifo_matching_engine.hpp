#include "matching_engine.hpp"
#include "slick/logger.hpp"
#include <common/order.hpp>

namespace slick::orderbook::detail {
    struct PriceLevelL3;
}

namespace slick::sim::engine {

using Logger = slick::logger::Logger;

class FifoMatchingEngine : public MatchingEngine {
public:
    FifoMatchingEngine(
        slick::SlickQueue<OrderResponse> &order_response_queue
    );
    ~FifoMatchingEngine() override = default;

    MatchingEngine::Type type() const noexcept override {
        return MatchingEngine::Type::FIFO;
    }

    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(Order* order, price_t order_price, qty_t order_qty, OrderBook &book, time_t request_time, time_t event_time, uint64_t seq_num = 0, SelfMatchPreventionMode smp_mode = SelfMatchPreventionMode::NONE) override;
    std::vector<TradeSummaryInfo> match(Side side, uint64_t order_id, price_t price, qty_t &qty, OrderBook &book, time_t event_time, uint64_t seq_num = 0) override;
};

    
} // namespace slick::sim
