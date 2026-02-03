#pragma once

#include <cstdint>
#include <common/messages.hpp>
#include <slick/queue.h>

namespace slick::sim::md_publisher {

class Publisher {
public:
    Publisher(slick::SlickQueue<Request> &request_queue, slick::SlickQueue<uint8_t> &market_data_queue)
        : request_queue_(request_queue), market_data_queue_(market_data_queue)
    {}

    virtual ~Publisher() = default;
    virtual void start() = 0;
    virtual void stop() = 0;

protected:
    slick::SlickQueue<Request> &request_queue_;
    slick::SlickQueue<uint8_t> &market_data_queue_;
};

}   // end namespace slick::sim::md_publisher