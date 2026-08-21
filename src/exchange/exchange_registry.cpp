#include "exchange_registry.hpp"

using namespace slick::sim;
using namespace slick::sim::exch;

ExchangeRegistry &ExchangeRegistry::instance() {
    // Function-local, so it is constructed by whichever venue's namespace-scope
    // initialiser runs first. A namespace-scope registry would make that a coin
    // flip across translation units.
    static ExchangeRegistry registry;
    return registry;
}

bool ExchangeRegistry::add(std::string_view key, Venue venue, Factory factory) {
    entries_.push_back(Entry{std::string(key), venue, factory});
    return true;
}

ExchangeRegistry::Factory ExchangeRegistry::find(std::string_view key) const noexcept {
    for (const auto &entry : entries_) {
        // The same comparison to_venue uses, so a config key resolves identically
        // whether it is being turned into a Venue or into a factory.
        if (detail::iequals(entry.key, key)) {
            return entry.factory;
        }
    }
    return nullptr;
}

std::vector<std::string> ExchangeRegistry::keys() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto &entry : entries_) {
        out.push_back(entry.key);
    }
    return out;
}
