#pragma once

#include "order_gateway.hpp"
#include <uwebsockets/App.h>
#include <string>
#include <thread>
#include <cstdint>

struct us_listen_socket_t;

namespace slick::sim::order_gateway {

class RestWsOrderGateway : public OrderGateway {

protected:
    std::thread thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uWS::Loop* loop_ = nullptr;
    uint32_t port_;

public:
    RestWsOrderGateway(
        Venue venue,
        slick::queue<Request>& request_queue,
        slick::queue<OrderResponse>& response_queue,
        uint32_t port
    )
        : OrderGateway(venue, request_queue, response_queue)
        , port_(port)
    {}

    ~RestWsOrderGateway() override {
        stop();
    }

    virtual std::string_view type() const noexcept = 0;

    void start() override;

    void stop() override {
        if (listen_socket_ && loop_) {
            // Closing the listen socket must happen on the loop's own thread;
            // doing it cross-thread races with the loop's poll handles.
            loop_->defer([this]() {
                if (listen_socket_) {
                    us_listen_socket_close(0, listen_socket_);
                    listen_socket_ = nullptr;
                }
            });
        }
        if (thread_.joinable()) thread_.join();
    }

protected:
    virtual void setup_routes(uWS::App &app) = 0;
    virtual void on_listen_socket([[maybe_unused]] us_listen_socket_t *listen_socket) {}
};

}   // end namespace slick::sim::order_gateway