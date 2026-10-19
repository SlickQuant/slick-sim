#pragma once

#include <common/types.hpp>
#include <string>

namespace slick::sim::md_publisher {

/// Formats a fixed-point price or size as Hyperliquid does on the wire.
///
/// The venue uses shortest-round-trip floats with at least one fractional digit:
/// 77052 becomes "77052.0", while 0.73245 and 1.915 keep their existing digits.
/// Use to_fixed_string with a one-digit minimum. In a session capture, all 201
/// distinct prices ended in ".0" and none of the 8807 distinct sizes had a
/// trailing zero.
///
/// This matters for clients that key levels by the received price string.
/// Coinbase uses to_price_string/to_qty_string instead and renders whole values
/// without the fractional digit, such as "99".
inline std::string to_hyperliquid_number(int_fast64_t value) {
    return to_fixed_string(value, /*min_frac_digits=*/1);
}

}   // end namespace slick::sim::md_publisher
