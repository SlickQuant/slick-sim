#pragma once

#include <slick/queue.h>
#include "message_parser.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <format>
#include <common/messages.hpp>

namespace slick::sim::order_gateway {

class OrderGateway {
public:
    explicit OrderGateway(
        Venue venue,
        slick::queue<Request> &request_queue,
        slick::queue<OrderResponse> &response_queue
    )
        : venue_(venue)
        , request_queue_(request_queue)
        , response_queue_(response_queue)
    {}

    virtual ~OrderGateway() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    
    // Order queue interface
    slick::queue<Request>& get_request_queue() { return request_queue_; }
    slick::queue<OrderResponse>& get_response_queue() { return response_queue_; }
    
protected:
    Venue venue_;
    // Order processing queue
    slick::queue<Request> &request_queue_;
    slick::queue<OrderResponse> &response_queue_;
};

} // namespace slick::sim::order_gateway