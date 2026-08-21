#pragma once

#include <common/types.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Deliberately the forward header: this is included by main.cpp and by every
// venue registration TU, none of which need the full nlohmann::json definition.
#include <nlohmann/json_fwd.hpp>

namespace slick::sim::exch {

class Exchange;

/// Maps a key under the config's `exchanges` object (e.g. "coinbase") to the
/// adapter that serves it, so no venue name appears in main.cpp or in any core
/// target. Each venue library registers itself at static-init time via
/// SLICK_SIM_REGISTER_VENUE below.
///
/// Populated once at startup and read once per configured exchange, so nothing
/// here is on a hot path: a flat vector beats a hash map at this size and keeps
/// the header free of <unordered_map>.
class ExchangeRegistry {
public:
    /// A plain function pointer, not std::function - no allocation, no <functional>.
    using Factory = std::unique_ptr<Exchange> (*)(const nlohmann::json &config);

    static ExchangeRegistry &instance();

    /// `key` is matched case-insensitively, the same way `to_venue` matches a
    /// venue name. Returns true so a registration can initialise a namespace-scope
    /// constant.
    bool add(std::string_view key, Venue venue, Factory factory);

    /// nullptr when no adapter for this key was built into the binary.
    Factory find(std::string_view key) const noexcept;

    /// Registered keys, in registration order - for the startup banner and for the
    /// error message on an unknown key.
    std::vector<std::string> keys() const;

private:
    ExchangeRegistry() = default;

    struct Entry {
        std::string key;
        Venue venue;
        Factory factory;
    };
    std::vector<Entry> entries_;
};

}   // end namespace slick::sim::exch

/// Defines a venue's factory and registers it. Place at namespace scope in exactly
/// one translation unit of the venue library:
///
///     SLICK_SIM_REGISTER_VENUE("coinbase", Venue::COINBASE, CoinbaseExchange);
///
/// That TU must live in an OBJECT library - see cmake/SlickSimVenue.cmake for why
/// a static library would let the linker drop this registration entirely.
#define SLICK_SIM_REGISTER_VENUE(key, venue_enum, ExchangeType)                  \
    namespace {                                                                  \
    ::std::unique_ptr<::slick::sim::exch::Exchange>                              \
    slick_sim_make_##ExchangeType(const ::nlohmann::json &config) {              \
        return ::std::make_unique<ExchangeType>(config);                         \
    }                                                                            \
    [[maybe_unused]] const bool slick_sim_registered_##ExchangeType =            \
        ::slick::sim::exch::ExchangeRegistry::instance().add(                    \
            key, venue_enum, &slick_sim_make_##ExchangeType);                    \
    }                                                                            \
    static_assert(true, "require a trailing semicolon")
