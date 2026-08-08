#pragma once

#include <limits>
#include <cstdint>
#include <string_view>

namespace slick::sim {

using price_t = int_fast64_t;
using qty_t = int_fast64_t;
using time_t = uint_fast64_t;
using symid_t = uint16_t;

constexpr symid_t INVALID_SYMBOL_ID = std::numeric_limits<symid_t>::max();
constexpr price_t NULL_PRICE = std::numeric_limits<price_t>::max();
constexpr int32_t DOUBLE_MULTIPLIER = static_cast<int32_t>(1e8);
constexpr double EPSILON = 1e-8;

inline constexpr price_t to_price_t(double price) {
    return static_cast<price_t>(price * DOUBLE_MULTIPLIER);
}   

inline constexpr double to_price_double(price_t price) {
    return static_cast<double>(price) / DOUBLE_MULTIPLIER;
}

inline constexpr qty_t to_qty_t(double qty) {
    return static_cast<qty_t>(qty * DOUBLE_MULTIPLIER);
}

inline constexpr double to_qty_double(qty_t qty) {
    return static_cast<double>(qty) / DOUBLE_MULTIPLIER;
}

enum Venue : uint8_t {
    UNKNOWN_VENUE,
    CME,
    ICE,
    COINBASE,
    HYPERLIQUID,
    STOCK,
    __COUNT__,  // for internal use only
};

inline const char* to_string(Venue venue) {
    switch(venue) {
    case Venue::CME:
        return "CME";
    case Venue::ICE:
        return "ICE";
    case Venue::COINBASE:
        return "COINBASE";
    case Venue::HYPERLIQUID:
        return "HYPERLIQUID";
    case Venue::STOCK:
        return "STOCK";
    case Venue::__COUNT__:
        break;
    }
    return "UNKNOWN";
}

inline Venue to_venue(std::string_view exch) {
    if (exch == "CME" || exch == "cme") {
        return Venue::CME;
    }

    if (exch == "ICE" || exch == "ice") {
        return Venue::ICE;
    }

    if (exch == "COINBASE" || exch == "coinbase") {
        return Venue::COINBASE;
    }

    if (exch == "HYPERLIQUID" || exch == "hyperliquid") {
        return Venue::HYPERLIQUID;
    }

    if (exch == "STOCK" || exch == "stock") {
        return Venue::STOCK;
    }

    return Venue::UNKNOWN_VENUE;
}

}   // end namespace slick::sim