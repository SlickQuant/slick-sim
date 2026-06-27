#pragma once

#include "market_data_publisher.hpp"
#include <uwebsockets/App.h>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <common/market_data.hpp>

namespace slick::sim::md_publisher {

class WebsocketMarketDataPublisher : public MarketDataPublisher {

public:
    WebsocketMarketDataPublisher(
        Venue venue,
        slick::queue<Request>& request_queue,
        slick::queue<uint8_t>& market_data_queue,
        uint32_t port
    );

    ~WebsocketMarketDataPublisher() override = default;

    void start() override;
    void stop() override;

protected:
    void schedule_publish_processing();
    void publish_processing();

    virtual void setup_routes(uWS::App &app) = 0;
    virtual void check_heartbeats() = 0;
    virtual void publish_market_data_update(MarketDataUpdate *update) = 0;

protected:
    std::thread ws_thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uWS::Loop* loop_ = nullptr;
    us_timer_t* heartbeat_timer_ = nullptr;
    uint32_t port_;
    std::atomic_bool running_{false};
    uint64_t data_cursor_ = 0;
    int ping_interval_ms_{15000};
    std::chrono::seconds pong_timeout_{60};
};

}