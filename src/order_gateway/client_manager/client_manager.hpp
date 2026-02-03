#pragma once

#include <slick/logger.hpp>
#include <slick/queue.h>
#include <thread>
#include <common/order.hpp>
#include <common/messages.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace slick::sim::order_gateway {

class ClientManager {

protected:
    json config_;
    slick::SlickQueue<Request> &request_queue_;
    slick::SlickQueue<OrderResponse> &response_queue_;
    Venue venue_;

public:
    ClientManager(const json& config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<OrderResponse> &response_queue)
        : config_(config)
        , request_queue_(request_queue)
        , response_queue_(response_queue)
        , venue_(to_venue(config["exchange"].template get<std::string_view>()))
    {
    }

    virtual ~ClientManager() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
};

} // namespace slick::sim::order_gateway