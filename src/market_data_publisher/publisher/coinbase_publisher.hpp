#pragma once

#include "publisher.hpp"
#include <uwebsockets/App.h> 
#include <coinbase/websocket.hpp>
#include <nlohmann/json.hpp>
#include <common/market_data.hpp>
#include <unordered_set>
#include <unordered_map>
#include <vector>

struct us_listen_socket_t;

namespace slick::sim::exch {
    struct Symbol;
}

namespace slick::sim::md_publisher {

using json = nlohmann::json;

// Per-socket data structure for uWebSockets
struct PerSocketData {
    uint64_t seq_num;
    std::chrono::steady_clock::time_point last_pong;  // Track last pong received
    bool subscribed_heartbeat = false;
    static uint_fast64_t heartbeat_count;

    struct SubscriptionInfo {
        std::unordered_set<std::string> active_subscriptions;
        std::unordered_set<std::string> pending_subscriptions;
        std::vector<std::string> received_subscriptions;
    };
    std::array<SubscriptionInfo, (uint8_t)coinbase::WebSocketChannel::__COUNT__> subscription_info;
};

class CoinbasePublisher : public Publisher {
    using wsT = uWS::WebSocket<false, true, PerSocketData>;

public:
    CoinbasePublisher(const json &config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<uint8_t> &market_data_queue);
    void start() override;
    void stop() override;

private:
    void schedulePublishProcessing();
    void publishMarketDataUpdate();
    void handleMessage(wsT *ws, std::string_view message);
    void publishSubscriptionResponse(MarketDataUpdate *update);
    void publishLevelSnapshot(MarketDataUpdate *update);
    void publishLevelUpdate(MarketDataUpdate *update);
    void publishOrderUpdate(MarketDataUpdate *update);
    // void publishTradeUpdate(MarketDataUpdate *update);
    void checkHeartbeats();
    void handleClose(wsT *ws, int code, std::string_view message);
    void sendError(wsT *ws, const std::string& error_msg);
    void unsubscribeMD(wsT *ws);

private:
    std::thread ws_thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uWS::Loop* loop_ = nullptr;
    us_timer_t* heartbeat_timer_ = nullptr;
    uint32_t port_ = 5000;
    std::atomic<bool> running_{false};

    uint64_t data_cursor_ = 0;

    std::unordered_set<wsT*> clients_;
    std::unordered_map<std::string, std::unordered_multimap<uint8_t, wsT*>> symbol_channel_client_;
    std::array<std::unordered_map<std::string, std::unordered_set<wsT*>>, (uint8_t)coinbase::WebSocketChannel::__COUNT__> subscription_info_;

    // Heartbeat configuration
    int ping_interval_ms_{15000};  // Send ping every 30 seconds (in milliseconds)
    std::chrono::seconds pong_timeout_{60};   // Disconnect if no pong for 60 seconds
};

}   // end namespace slick::sim::md_publisher