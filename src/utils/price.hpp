#pragma once

#include <common/order.hpp>

namespace slick::sim::utils {

template<Side s>
inline bool isPriceBetter(price_t a, price_t b) {
    if constexpr (s == Side::BUY) {
        return a > b;
    } else {
        return a < b;
    }
}

template<Side s>
inline bool isPriceBetterOrEqual(price_t a, price_t b) {
    if constexpr (s == Side::BUY) {
        return a >= b;
    } else {
        return a <= b;
    }
}

inline bool isPriceBetter(Side side, price_t a, price_t b) {
    if (side == Side::BUY) {
        return a > b;
    } else {
        return a < b;
    }
}

inline bool isPriceBetterOrEqual(Side side, price_t a, price_t b) {
    if (side == Side::BUY) {
        return a >= b;
    } else {
        return a <= b;
    }
}

}