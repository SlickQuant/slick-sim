#pragma once

#include <cstdint>
#include <slick/queue.h>
#include <common/types.hpp>
#include <common/messages.hpp>

namespace slick::sim::md_publisher {

class MarketDataPublisher {
public:
    MarketDataPublisher(Venue venue, slick::queue<Request> &request_queue, slick::queue<uint8_t> &md_queue)
        : venue_(venue)
        , request_queue_(request_queue)
        , md_queue_(md_queue)
    {}

    virtual ~MarketDataPublisher() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

protected:
    Venue venue_;
    slick::queue<Request> &request_queue_;
    slick::queue<uint8_t> &md_queue_;
};

}