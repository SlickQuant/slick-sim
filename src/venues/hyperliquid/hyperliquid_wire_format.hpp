#pragma once

#include <common/types.hpp>
#include <string>

namespace slick::sim::md_publisher {

/// Renders a fixed-point price or size the way Hyperliquid puts one on the wire.
///
/// The venue serialises both as shortest-round-trip floats, which always leave a
/// fractional digit behind: 77052 goes out as "77052.0". Nothing is padded past
/// that - 0.73245 stays "0.73245" and 1.915 stays "1.915" - so this is exactly
/// `to_fixed_string` with a one-digit floor, not a fixed-decimal format. A
/// session capture bears that out: all 201 distinct prices ended in ".0", and
/// not one of the 8807 distinct sizes carried a trailing zero.
///
/// Worth reproducing because a client may key levels by the price string it
/// received rather than by a parsed number, and against the venue those keys
/// carry the ".0".
///
/// Hyperliquid's only, deliberately: Coinbase renders the same values with
/// `to_price_string`/`to_qty_string`, which trim a whole number down to "99".
inline std::string to_hyperliquid_number(int_fast64_t value) {
    return to_fixed_string(value, /*min_frac_digits=*/1);
}

}   // end namespace slick::sim::md_publisher
