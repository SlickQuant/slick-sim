#pragma once

#include <limits>
#include <cstdint>

namespace slick::sim {

using price_t = int_fast64_t;
using qty_t = uint_fast64_t;
using time_t = uint_fast64_t;
using symid_t = uint16_t;

constexpr price_t NULL_PRICE = std::numeric_limits<price_t>::max();
constexpr int32_t DOUBLE_MULTIPLIER = 1e8;

enum Venue : uint8_t {
    UNKNOWN_VENUE,
    CME,
    ICE,
    COINBASE,
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

    if (exch == "STOCK" || exch == "stock") {
        return Venue::STOCK;
    }

    return Venue::UNKNOWN_VENUE;
}

}   // end namespace slick::sim