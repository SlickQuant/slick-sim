#pragma once

#include "ws_md_publisher.hpp"
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

class HyperliquidPublisher : public WebsocketMarketDataPublisher {
    using wsT = uWS::WebSocket<false, true, HyperliquidPerSocketData>;

public:
    HyperliquidPublisher(const json& config,
                         slick::queue<Request>& request_queue,
                         slick::queue<uint8_t>& market_data_queue);
    // void start() override;
    // void stop() override;

private:
    void setup_routes(uWS::App &app) override;
    // void schedulePublishProcessing();
    void publish_market_data_update(MarketDataUpdate* update);
    void handle_message(wsT* ws, std::string_view message);
    void publish_subscription_response(MarketDataUpdate* update);
    void publish_book_snapshot(MarketDataUpdate* update);
    void publish_trade_update(MarketDataUpdate* update);
    void check_heartbeats() override;
    void handle_close(wsT* ws, int code, std::string_view message);
    void send_error(wsT* ws, const std::string& error_msg);
    void unsubscribe_md(wsT* ws);
    void subscribe_channel(wsT* ws, HyperliquidChannel channel, const std::string& coin);

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
