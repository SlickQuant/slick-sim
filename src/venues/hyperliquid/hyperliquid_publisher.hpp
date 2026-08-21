#pragma once

#include <market_data_publisher/ws_md_publisher.hpp>
#include "hyperliquid_l2_diff.hpp"
#include "hyperliquid_channels.hpp"
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
#include <utility>

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

private:
    void setup_routes(uWS::App &app) override;
    void publish_market_data_update(MarketDataUpdate* update) override;
    void handle_message(wsT* ws, std::string_view message);
    void handle_info(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void publish_subscription_response(MarketDataUpdate* update);
    void publish_book_snapshot(MarketDataUpdate* update);
    void publish_trade_summary(MarketDataUpdate* update);
    void check_heartbeats() override;
    void handle_close(wsT* ws, int code, std::string_view message);
    void send_error(wsT* ws, const std::string& error_msg);
    void unsubscribe_md(wsT* ws);
    void subscribe_channel(wsT* ws, HyperliquidChannel channel, const std::string& coin);

private:
    // ws_thread_, listen_socket_, loop_, heartbeat_timer_, port_, running_,
    // data_cursor_, ping_interval_ms_ and pong_timeout_ all live in
    // WebsocketMarketDataPublisher. Redeclaring them here shadowed the base
    // copies - the ones the inherited start()/stop() actually drive - so
    // check_heartbeats() was reading a second, independent pong_timeout_ that
    // merely happened to share the default. CoinbasePublisher was already
    // cleaned up this way.
    std::string upstream_base_url_;

    std::unordered_set<wsT*> clients_;
    // symbol -> multimap<channel, ws>
    std::unordered_map<std::string, std::unordered_multimap<uint8_t, wsT*>> symbol_channel_client_;
    // channel -> symbol -> set<ws>
    std::array<std::unordered_map<std::string, std::unordered_set<wsT*>>,
               static_cast<uint8_t>(HyperliquidChannel::__COUNT__)> subscription_info_;
    // coin -> (bid, ask) — last book state broadcast to "l2" subscribers, used to compute the next diff
    std::unordered_map<std::string, std::pair<L2Levels, L2Levels>> l2_diff_baseline_;
};

}   // end namespace slick::sim::md_publisher
