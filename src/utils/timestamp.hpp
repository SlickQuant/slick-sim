#pragma once

#include <string>
#include <chrono>
#include <format>

namespace slick::sim::utils {

inline std::string format_timestamp_iso8601(const std::chrono::system_clock::time_point& tp, int num_sebsecond_digits = 3) {
    auto utc_time = std::chrono::clock_cast<std::chrono::utc_clock>(tp);
    auto seconds = std::chrono::floor<std::chrono::seconds>(utc_time);

    if (num_sebsecond_digits > 6) {
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(utc_time - seconds);
        return std::format("{:%Y-%m-%dT%H:%M:%S}.{:09d}Z", seconds, nanoseconds.count());
    }
    else if (num_sebsecond_digits > 3) {
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(utc_time - seconds);
        return std::format("{:%Y-%m-%dT%H:%M:%S}.{:06d}Z", seconds, microseconds.count());
    }
    else if (num_sebsecond_digits > 0) {
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(utc_time - seconds);
        return std::format("{:%Y-%m-%dT%H:%M:%S}.{:03d}Z", seconds, milliseconds.count());
    }
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", seconds);
}

inline std::string format_timestamp_iso8601(uint64_t timestamp, int num_subsecond_digits = 3) {
    std::chrono::system_clock::time_point tp;

    // Heuristic to determine timestamp unit based on magnitude:
    // - Milliseconds: Year 2100 = ~4,102,444,800,000 (4.1 trillion)
    // - Microseconds: Year 2100 = ~4,102,444,800,000,000 (4.1 quadrillion)
    // - Nanoseconds: Year 2100 = ~4,102,444,800,000,000,000 (4.1 quintillion)
    constexpr uint64_t YEAR_2100_MS = 4'102'444'800'000ULL;
    constexpr uint64_t YEAR_2100_US = 4'102'444'800'000'000ULL;

    if (timestamp < YEAR_2100_MS) {
        // Timestamp is in milliseconds
        auto ms = std::chrono::milliseconds{timestamp};
        tp = std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(ms)};
    } else if (timestamp < YEAR_2100_US) {
        // Timestamp is in microseconds
        auto us = std::chrono::microseconds{timestamp};
        tp = std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(us)};
    } else {
        // Timestamp is in nanoseconds
        auto ns = std::chrono::nanoseconds{timestamp};
        tp = std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(ns)};
    }

    return format_timestamp_iso8601(tp, num_subsecond_digits);
}

inline std::string format_timestamp_iso8601(int num_sebsecond_digits = 3) {
    auto now = std::chrono::system_clock::now();
    return format_timestamp_iso8601(now, num_sebsecond_digits);
}

inline std::string format_heatbeat_timestamp() {
    static std::chrono::system_clock::time_point start_time = std::chrono::system_clock::now();
    // Get time since epoch
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    
    // Convert to time_t for formatting date/time
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    
    // Calculate monotonic time (time since start)
    auto elapsed = now - start_time;
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    auto elapsed_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - elapsed_seconds);
    
    // Format the string
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:09d} +0000 UTC m=+{}.{:09d}",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<int>(nanoseconds.count()),
        elapsed_seconds.count(),
        static_cast<int>(elapsed_nanos.count()));
}

inline uint64_t get_current_time_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

constexpr uint64_t ONE_SECOND_NS = 1'000'000'000;
constexpr uint64_t ONE_MILLISECOND_NS = 1'000'000;
constexpr uint64_t ONE_MICROSECOND_NS = 1'000;

}   // namespace slick::sim::utils
