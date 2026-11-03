#pragma once

#include <cassert>
#include <cstdint>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#elif !defined(__SIZEOF_INT128__)
#include <common/types.hpp>
#endif

namespace slick::sim::utils {

/// floor(a * b / c), with the product held at 128 bits. Fixed-point quantities
/// carry a 1e8 scale, so the product of two of them overflows 64 bits.
/// Requires b <= c: the result is then at most a and always fits, which is also
/// _udiv128's precondition (it faults on a quotient over 64 bits).
/// MSVC x64 and GCC/Clang take the first two branches; the boost fallback (32-bit
/// MSVC, compilers without __int128) is not built by any CI job.
inline uint64_t mul_div_floor(uint64_t a, uint64_t b, uint64_t c) noexcept {
    assert(c != 0 && b <= c);
#if defined(_MSC_VER) && defined(_M_X64)
    uint64_t hi;
    const uint64_t lo = _umul128(a, b, &hi);
    uint64_t rem;
    return _udiv128(hi, lo, c, &rem);
#elif defined(__SIZEOF_INT128__)
    __extension__ typedef unsigned __int128 u128;   // __extension__: no -Wpedantic warning
    return static_cast<uint64_t>(static_cast<u128>(a) * b / c);
#else
    return static_cast<uint64_t>(cum_value_t(a) * b / c);
#endif
}

}   // namespace slick::sim::utils
