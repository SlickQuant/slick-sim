#pragma once

#include "order_book.hpp"
#include <coinmon/order.hpp>

namespace slick::sim::order_book {

template<Side SIDE>
class L3OrderBoolSideLevel : public OrderBookSideLevel<SIDE> {
    typedef typename std::conditional<side == rhone::common::Side::BUY, G, L>::type CMP;
    using Levels = rhone::util::PoolAllocatedMap<price_t, Level, 100000, CMP>;
    std::set<Order*> orders_;
public:
    using OrderBookSideLevel<SIDE>::OrderBookSideLevel;

    void add_order(const Order *order) {
        orders_.insert(order);
        this->size_ += static_cast<uint_fast64_t>(order->qty * DOUBLE_MULTIPLIER);
        this->num_orders_ = static_cast<uint32_t>(orders_.size());
    }

    void remove_order(const Order& order) {
        auto it = orders_.find(order);
        if (it != orders_.end()) {
            this->size_ -= static_cast<uint_fast64_t>(it->size * DOUBLE_MULTIPLIER);
            orders_.erase(it);
            this->num_orders_ = static_cast<uint32_t>(orders_.size());
        }
    }

    const std::set<Order>& get_orders() const {
        return orders_;
    }
};


}   // end namespace slick::sim::order_book