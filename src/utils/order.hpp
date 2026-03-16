#pragma once

#include <cstdint>
#include <atomic>

namespace slick::sim::utils {

inline uint64_t nextOrderId() noexcept {
    static std::atomic<uint64_t> next_order_id{1};
    return next_order_id.fetch_add(1, std::memory_order_relaxed);
}

}