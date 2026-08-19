#pragma once

#include <limits>
#include <cstdint>
#include <string>
#include <string_view>
#include <boost/multiprecision/cpp_int.hpp>

namespace slick::sim {

using price_t = int_fast64_t;
using qty_t = int_fast64_t;
using time_t = uint_fast64_t;
using symid_t = uint16_t;
using cum_value_t = boost::multiprecision::uint128_t;

constexpr symid_t INVALID_SYMBOL_ID = std::numeric_limits<symid_t>::max();
constexpr price_t NULL_PRICE = std::numeric_limits<price_t>::max();
constexpr int32_t DOUBLE_MULTIPLIER = static_cast<int32_t>(1e8);
constexpr double EPSILON = 1e-8;

/// Scales a decimal into the fixed-point representation, rounding to the nearest
/// tick rather than truncating.
///
/// `price * 1e8` is a binary floating-point multiply, and most decimal fractions
/// have no exact binary form, so the product lands just under the intended integer:
/// 8.2 * 1e8 is 819999999.99999988, and a plain cast to an integer throws the
/// fraction away, yielding 819999999 - a price of 8.19999999. The same happens to
/// 0.29 and 4.35, and to quantities. Adding half a tick before truncating rounds to
/// nearest instead, which lands on the intended value for every decimal that fits
/// in the scale.
inline constexpr price_t to_price_t(double price) {
    return static_cast<price_t>(price * DOUBLE_MULTIPLIER + (price < 0 ? -0.5 : 0.5));
}

inline constexpr double to_price_double(price_t price) {
    return static_cast<double>(price) / DOUBLE_MULTIPLIER;
}

inline constexpr qty_t to_qty_t(double qty) {
    return static_cast<qty_t>(qty * DOUBLE_MULTIPLIER + (qty < 0 ? -0.5 : 0.5));
}

inline constexpr double to_qty_double(qty_t qty) {
    return static_cast<double>(qty) / DOUBLE_MULTIPLIER;
}

/// Renders a fixed-point value as an exact decimal string.
///
/// Formats straight from the integer, never through double. `std::to_string(double)`
/// formats with `%f`, which emits exactly six fractional digits, but the scale here
/// carries eight: a size of 0.00011628 went onto the wire as "0.000116", and one
/// tick, 0.00000001, as "0.000000" - a value the receiver reads as zero.
///
/// Trailing fractional zeros are trimmed, and the point is omitted entirely for a
/// whole number, so 8.2 renders as "8.2" rather than "8.20000000".
inline std::string to_fixed_string(int_fast64_t value) {
    const bool negative = value < 0;
    // Negated through the unsigned domain so the most negative value cannot overflow.
    const uint64_t magnitude = negative
        ? (0ULL - static_cast<uint64_t>(value))
        : static_cast<uint64_t>(value);

    const uint64_t scale = static_cast<uint64_t>(DOUBLE_MULTIPLIER);
    uint64_t fraction = magnitude % scale;

    std::string out;
    if (negative) {
        out.push_back('-');
    }
    out += std::to_string(magnitude / scale);

    if (fraction == 0) {
        return out;
    }

    char digits[8];
    for (int i = 7; i >= 0; --i) {
        digits[i] = static_cast<char>('0' + fraction % 10);
        fraction /= 10;
    }
    std::string_view significant(digits, 8);
    while (!significant.empty() && significant.back() == '0') {
        significant.remove_suffix(1);
    }

    out.push_back('.');
    out.append(significant);
    return out;
}

inline std::string to_price_string(price_t price) { return to_fixed_string(price); }
inline std::string to_qty_string(qty_t qty) { return to_fixed_string(qty); }

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
    case Venue::UNKNOWN_VENUE:
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