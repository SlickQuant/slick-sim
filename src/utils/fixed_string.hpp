#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

namespace slick::sim::utils {

/// Copies a name into a fixed-width wire field.
///
/// Truncates to N-1 characters and zero-fills the remainder. The result is always
/// NUL-terminated, with no bytes copied from beyond the source string.
///
/// Copying sizeof(dst) bytes from a shorter string would over-read it and could
/// change the value the receiver sees. Zeroing dst before copying up to N bytes
/// also fails for an N-character value, which overwrites the terminator.
///
/// Returns true when the value did not fit, so callers can warn about truncation.
template<size_t N>
inline bool copy_wire_field(char (&dst)[N], std::string_view src) noexcept {
    const auto len = std::min(src.size(), N - 1);
    if (len) {
        // Guarded because a default-constructed string_view has a null data()
        // pointer, and memcpy from null is undefined even for zero bytes.
        std::memcpy(dst, src.data(), len);
    }
    std::memset(dst + len, 0, N - len);
    return len < src.size();
}

/// A null-terminated character buffer of fixed capacity, stored inline.
///
/// Order and Symbol use it for identity fields bounded by their wire formats.
/// These fields are written once and read often; inline storage avoids heap
/// allocations on pooled objects in the order path.
///
/// The layout is exactly char[N], with no size field or pointer. Consecutive
/// fields therefore match a wire header byte for byte and can be copied with
/// one memcpy. size() scans at most N bytes with strnlen, so avoid calling it
/// repeatedly in a hot loop.
///
/// Values are truncated to N-1 characters, matching the wire fields' limit.
template<size_t N>
class fixed_string {
    static_assert(N > 1, "fixed_string needs room for at least one character and a terminator");

    char data_[N]{};

public:
    fixed_string() = default;
    fixed_string(std::string_view value) noexcept { assign(value); }
    fixed_string(const char *value) noexcept { assign(value ? std::string_view(value) : std::string_view()); }

    fixed_string &operator=(std::string_view value) noexcept { assign(value); return *this; }
    fixed_string &operator=(const char *value) noexcept {
        assign(value ? std::string_view(value) : std::string_view());
        return *this;
    }

    /// Truncates anything that does not fit and zero-fills the tail, so the buffer
    /// is always null-terminated and a recycled object can never leak the previous
    /// value past the terminator.
    void assign(std::string_view value) noexcept { copy_wire_field(data_, value); }

    void clear() noexcept { std::memset(data_, 0, N); }

    const char *c_str() const noexcept { return data_; }
    const char *data() const noexcept { return data_; }
    char *data() noexcept { return data_; }

    size_t size() const noexcept { return ::strnlen(data_, N); }
    bool empty() const noexcept { return data_[0] == '\0'; }
    static constexpr size_t capacity() noexcept { return N - 1; }
    static constexpr size_t buffer_size() noexcept { return N; }

    std::string_view view() const noexcept { return std::string_view(data_, size()); }
    operator std::string_view() const noexcept { return view(); }
    std::string str() const { return std::string(view()); }

    friend bool operator==(const fixed_string &lhs, std::string_view rhs) noexcept {
        return lhs.view() == rhs;
    }
    friend bool operator==(const fixed_string &lhs, const fixed_string &rhs) noexcept {
        return std::memcmp(lhs.data_, rhs.data_, N) == 0;
    }

    friend std::ostream &operator<<(std::ostream &os, const fixed_string &value) {
        return os << value.view();
    }
};

/// Transparent hash/equality for maps keyed by fixed_string.
///
/// `is_transparent` is what lets a lookup take a `std::string_view` (or a bare
/// `const char*` from a wire field) without materialising the key type first, so
/// the common already-present case costs no construction at all. fixed_string
/// converts to string_view implicitly, so keys hash identically either way.
struct StringViewHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct StringViewEq {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

} // namespace slick::sim::utils

/// Lets fixed_string drop into std::format and therefore into every LOG_* call,
/// which slick-logger routes through std::format.
template<size_t N, class CharT>
struct std::formatter<slick::sim::utils::fixed_string<N>, CharT> : std::formatter<std::string_view, CharT> {
    template<class FormatContext>
    auto format(const slick::sim::utils::fixed_string<N> &value, FormatContext &ctx) const {
        return std::formatter<std::string_view, CharT>::format(value.view(), ctx);
    }
};
