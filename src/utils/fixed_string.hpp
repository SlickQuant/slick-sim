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
/// Replaces the `memcpy(dst, name.c_str(), sizeof(dst))` idiom, which reads
/// sizeof(the field) bytes out of a buffer holding only as many characters as the
/// name actually has, and leaves whatever followed the name in the field. The
/// receiver reads these fields as C strings, so a non-zero byte landing after the
/// name is not merely an over-read - it changes the value that arrives.
///
/// Truncates to N-1 characters and zero-fills the remainder, so the field is
/// always terminated and never carries bytes from the source's neighbourhood.
///
/// The `memset(dst, 0, sizeof(dst))` + `memcpy(dst, s.c_str(), min(sizeof(dst), s.size()))`
/// pairing it also replaces gets the over-read right but not the terminator: a
/// value that exactly fills the field overwrites the last zero the memset put
/// there, and the reader then runs off the end of the field.
///
/// Returns true when the value did not fit, for callers that warn about it.
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
/// Stands in for `std::string` on the identity fields of `Order` and `Symbol`.
/// Those fields are bounded by the fixed-width wire formats that carry them, are
/// written once and read many times, and were costing a heap allocation each on
/// objects that are pooled precisely so the order path never allocates.
///
/// The layout is exactly `char[N]` - no size field, no pointer - so a run of
/// these matches a run of fixed-width wire fields byte for byte and a whole
/// message header can be filled with one memcpy. `size()` is a `strnlen` over at
/// most N bytes, which is why the type suits identity fields rather than anything
/// measured in a loop.
///
/// Values that do not fit are truncated to N-1 characters. That is not a new
/// limit: the wire fields these mirror have always truncated, and the previous
/// `memcpy(dst, str.c_str(), sizeof(dst))` idiom silently read past the end of
/// any shorter string to fill them.
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
