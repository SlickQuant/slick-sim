#pragma once

#include <cstdint>
#include <atomic>
#include <cassert>
#include <common/types.hpp>

namespace slick::sim::utils {

inline uint64_t nextOrderId() noexcept {
    static std::atomic<uint64_t> next_order_id{1};
    return next_order_id.fetch_add(1, std::memory_order_relaxed);
}

inline void executeOrder(Order* order, price_t price, qty_t executed_quantity, time_t timestamp) {
    assert(executed_quantity > 0);
    order->leaves_quantity -= executed_quantity;
    assert(order->leaves_quantity >= 0);
    order->last_update_time = timestamp;
    order->cum_value += static_cast<cum_value_t>(price) * static_cast<cum_value_t>(executed_quantity);
    order->cum_quantity += executed_quantity;
    order->avg_fill_price = order->cum_quantity > 0
        ? static_cast<price_t>(order->cum_value / static_cast<cum_value_t>(order->cum_quantity))
        : 0;
    order->last_fill_price = price;
    order->last_fill_qty = executed_quantity;
    order->last_fill_time = timestamp;
    order->num_fills += 1;
}

}