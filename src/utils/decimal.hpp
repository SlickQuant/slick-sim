#pragma once

#include <common/types.hpp>
#include <cstdint>

/// Conversions between wire decimals with a constant exponent of -9 (CME PRICE9,
/// ICE Decimal9) and the simulator's 1e-8 fixed point. Null values are each
/// protocol's own, so its codec maps them before calling these.
namespace slick::sim::utils {

/// A 1e-9 mantissa is one decimal place finer than the simulator's scale.
inline constexpr int64_t DECIMAL9_PER_FIXED = 1'000'000'000 / DOUBLE_MULTIPLIER;
static_assert(DECIMAL9_PER_FIXED == 10);

/// Returns false when the value has a digit at 1e-9. No listed tick or lot is that
/// fine, so such a value is off-tick, and rounding it would trade at a price or
/// size the client never sent.
[[nodiscard]] inline constexpr bool from_decimal9(int64_t mantissa, int64_t &out) noexcept {
    if (mantissa % DECIMAL9_PER_FIXED != 0) {
        return false;
    }
    out = mantissa / DECIMAL9_PER_FIXED;
    return true;
}

/// The inverse. A fixed-point value above 9.2e17 would overflow the multiply, but that
/// is 9.2e9 in price or size units - far past any tick, lot or level a wire decimal of
/// this exponent can carry.
[[nodiscard]] inline constexpr int64_t to_decimal9(int64_t fixed) noexcept {
    return fixed * DECIMAL9_PER_FIXED;
}

}   // namespace slick::sim::utils
