#pragma once

#include "publisher.hpp"
#include <exchange/exch_hyperliquid.hpp>
#include <uwebsockets/App.h>
#include <nlohmann/json.hpp>
#include <common/market_data.hpp>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <array>

struct us_listen_socket_t;

namespace slick::sim::md_publisher {

using json = nlohmann::json;
using HyperliquidChannel = slick::sim::exch::HyperliquidChannel;

struct HyperliquidPerSocketData {
    uint64_t seq_num = 0;
    std::chrono::steady_clock::time_point last_pong;
    bool subscribed_heartbeat = false;
    static uint_fast64_t heartbeat_count;

    struct SubInfo {
        std::unordered_set<std::string> active;
        std::unordered_set<std::string> pending;
        std::vector<std::string> received;
    };
    std::array<SubInfo, static_cast<uint8_t>(HyperliquidChannel::__COUNT__)> subs;
};

class HyperliquidPublisher : public Publisher {
    using wsT = uWS::WebSocket<false, true, HyperliquidPerSocketData>;

public:
    HyperliquidPublisher(const json& config,
                         slick::SlickQueue<Request>& request_queue,
                         slick::SlickQueue<uint8_t>& market_data_queue);
    void start() override;
    void stop() override;

private:
    void schedulePublishProcessing();
    void publishMarketDataUpdate();
    void handleMessage(wsT* ws, std::string_view message);
    void publishSubscriptionResponse(MarketDataUpdate* update);
    void publishBookSnapshot(MarketDataUpdate* update);
    void publishTradeUpdate(MarketDataUpdate* update);
    void checkHeartbeats();
    void handleClose(wsT* ws, int code, std::string_view message);
    void sendError(wsT* ws, const std::string& error_msg);
    void unsubscribeMD(wsT* ws);
    void subscribeChannel(wsT* ws, HyperliquidChannel channel, const std::string& coin);

private:
    std::thread ws_thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uWS::Loop* loop_ = nullptr;
    us_timer_t* heartbeat_timer_ = nullptr;
    uint32_t port_ = 5001;
    std::atomic<bool> running_{false};

    uint64_t data_cursor_ = 0;

    std::unordered_set<wsT*> clients_;
    // symbol -> multimap<channel, ws>
    std::unordered_map<std::string, std::unordered_multimap<uint8_t, wsT*>> symbol_channel_client_;
    // channel -> symbol -> set<ws>
    std::array<std::unordered_map<std::string, std::unordered_set<wsT*>>,
               static_cast<uint8_t>(HyperliquidChannel::__COUNT__)> subscription_info_;

    int ping_interval_ms_{15000};
    std::chrono::seconds pong_timeout_{60};
};

}   // end namespace slick::sim::md_publisher
