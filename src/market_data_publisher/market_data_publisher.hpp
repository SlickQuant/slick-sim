#pragma once

#include <string>
#include <memory>
#include <array>
#include <atomic>
#include <cstdint>
#include <slick/queue.h>
#include <common/messages.hpp>
#include <nlohmann/json.hpp>
#include "publisher/publisher.hpp"

namespace slick::sim::md_publisher {

class MarketDataPublisher {
public:
    MarketDataPublisher(const nlohmann::json &config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<uint8_t> &market_data_queue);
    ~MarketDataPublisher() = default;

    void start();
    void stop();

private:
    slick::SlickQueue<Request> &request_queue_;
    slick::SlickQueue<uint8_t> &market_data_queue_;
    std::array<std::unique_ptr<Publisher>, Venue::__COUNT__> publishers_;
    std::atomic_bool run_ = true;
};

}