#pragma once

#include <slick/queue.h>
#include <slick/object_pool.h>
#include <md_feed/md_feed.hpp>
#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <array>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <common/market_data.hpp>
#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>

namespace slick::sim::exch {

struct Symbol;

class Exchange {
public:
    Exchange(
        Venue venue,
        const nlohmann::json &config,
        slick::SlickQueue<Request> &request_queue,
        slick::SlickQueue<OrderResponse> &order_response_queue,
        slick::SlickQueue<uint8_t> &md_update_queue,
        uint_fast32_t buffer_size = 65536
    );

    virtual ~Exchange();
    virtual void run();
    void stop();

    
protected:
    Order* allocateOrder();
    void processMdData();
    void processRequest();
    void handleNewOrderRequest(const Request &request);
    void rejectMdSubscription(const Request &request, MDSubscriptionRejectReason reason);
    void rejectNewOrderRequest(const Request &request, OrdRejectReason reason);
    void rejectModifyOrderRequest(const Request &request, OrdRejectReason reason);
    void rejectCancelOrderRequest(const Request &request, OrdRejectReason reason);
    void sendOrderNewPending(const Order *order);
    void sendOrderAck(const Order *order);
    void sendOrderReplacePending(const Request &request);
    void sendOrderReplaced(const Order *order);
    void sendOrderCancelPending(const Request &request);
    void sendOrderCanceled(const Order *order);
    void sendOrderExecution(const Order *order);
    void publishMDBookUpdate(symid_t sid, OrderBook &order_book, const std::array<uint8_t, 2> &indices);
    void publishLevelUpdate(const char* symbol, const std::vector<MDLevel> &level_updates);
    void publishMDTrades(const char* symbol, const std::vector<MDTrade> &trade_updates);
    void publishTradeSummary(const char* symbol, const TradeSummaryInfo &trade_summary);

    virtual void handleMdSubscription(const Request &/* request */) {}
    virtual void handleMdUnsubscription(const Request &/* request */) {}

protected:
    Venue venue_;
    nlohmann::json config_;
    slick::SlickQueue<Request> &request_queue_;
    slick::SlickQueue<OrderResponse> &order_response_queue_;
    slick::SlickQueue<uint8_t> &md_update_queue_;
    std::shared_ptr<md_feed::MDFeed> md_feed_;
    std::thread thread_;
    std::thread md_thread_;
    std::atomic_bool run_{true};
    uint_fast64_t order_request_cursor_{0};
    std::array<std::unique_ptr<engine::MatchingEngine>, engine::MatchingEngine::Type::__count__> matching_engines_;
    slick::ObjectPool<Order> order_buffer_;
};

}   // end namespace slick::sim::exch