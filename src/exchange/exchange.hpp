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
#include <nlohmann/json.hpp>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <common/market_data.hpp>
#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <order_gateway/order_gateway.hpp>
#include <market_data_publisher/market_data_publisher.hpp>

namespace slick::sim::exch {

struct Symbol;

class Exchange {
public:
    Exchange(Venue venue, const nlohmann::json &config);

    virtual ~Exchange() {
        stop();
    }

    virtual void start();
    virtual void stop();

    queue<Request>& request_queue() noexcept {
        return request_queue_;
    }
    queue<OrderResponse>& response_queue() noexcept {
        return response_queue_;
    }
    queue<uint8_t>& md_queue() noexcept {
        return md_queue_;
    }
    
protected:
    void processRequest();
    void handleNewOrderRequest(const Request &request);
    void handleModifyOrderRequest(const Request &request);
    void handleCancelOrderRequest(const Request &request);
    void rejectMdSubscription(const Request &request, MDSubscriptionRejectReason reason);
    void rejectNewOrderRequest(const Request &request, OrdRejectReason reason);
    void rejectModifyOrderRequest(const Request &request, OrdRejectReason reason);
    void rejectCancelOrderRequest(const Request &request, OrdRejectReason reason);
    void sendOrderNewPending(const Order *order, time_t request_time);
    void sendOrderReplacePending(const Order *order, time_t request_time);
    void sendOrderCancelPending(const Order *order, time_t request_time);
    void publishMDBookUpdate(symid_t sid, OrderBook &order_book, const std::array<uint8_t, 2> &indices);
    void publishLevelUpdate(const char* symbol, const std::vector<MDLevel> &level_updates);
    void publishMDOrderUpdate(const char* symbol, const std::vector<MDOrder> &order_updates);
    void publishMDTrades(const char* symbol, const std::vector<MDTrade> &trade_updates);
    void publishTradeSummary(const char* symbol, const TradeSummaryInfo &trade_summary);

    virtual void handleMdSubscription(const Request &/* request */) {}
    virtual void handleMdUnsubscription(const Request &/* request */) {}


protected:
    Venue venue_;
    queue<Request> request_queue_;
    queue<OrderResponse> response_queue_;
    queue<uint8_t> md_queue_;
    std::shared_ptr<md_feed::MDFeed> md_feed_;
    std::thread thread_;
    std::thread md_thread_;
    std::atomic_bool run_{true};
    uint_fast64_t order_request_cursor_{0};
    std::array<std::unique_ptr<engine::MatchingEngine>, engine::MatchingEngine::Type::__count__> matching_engines_;
    std::vector<std::unique_ptr<order_gateway::OrderGateway>> order_gateways_;
    std::unique_ptr<md_publisher::MarketDataPublisher> md_publisher_;
};

}   // end namespace slick::sim::exch