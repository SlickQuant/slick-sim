#pragma once

#include <deque>
#include <string_view>
#include <unordered_map>
#include <exchange/symbol.hpp>
#include <common/market_data.hpp>
#include "types.hpp"

namespace slick::sim {

/// An instrument is identified by both venue and name. Using the name alone
/// would make instruments with the same ticker on different venues collide:
/// the second Symbol would be unreachable, and lookups would return the first.
struct SymbolKey {
    Venue venue;
    symbol_name_t name;

    bool operator==(const SymbolKey &other) const noexcept {
        return venue == other.venue && name == other.name;
    }
};

struct SymbolKeyHash {
    size_t operator()(const SymbolKey &key) const noexcept {
        return std::hash<std::string_view>{}(key.name.view()) * 31u
             + static_cast<size_t>(key.venue);
    }
};

class SymbolManager {
public:
    static SymbolManager& instance() {
        static SymbolManager instance;
        return instance;
    }

    SymbolManager(const SymbolManager&) = delete;
    SymbolManager(SymbolManager&&) = delete;
    SymbolManager& operator=(const SymbolManager&) = delete;
    SymbolManager& operator=(SymbolManager&&) = delete;

    /// A venue is required: see SymbolKey.
    exch::Symbol* getSymbol(std::string_view sym, Venue venue) const {
        // The inline name avoids allocating and freeing a string on each lookup,
        // including the lookup for every inbound order.
        auto it = symbols_by_name_.find(SymbolKey{venue, sym});
        if (it == symbols_by_name_.end()) {
            return nullptr;
        }
        return it->second;
    }

    exch::Symbol* getSymbol(symid_t sid) {
        if (sid >= symbols_.size()) {
            return nullptr;
        }
        return &symbols_[sid];
    }

    /// Returns the existing instrument if this (venue, name) already has one, so a
    /// double create cannot strand a Symbol that nothing can look up.
    exch::Symbol* createSymbol(std::string_view sym, Venue venue) {
        SymbolKey key{venue, sym};
        if (auto it = symbols_by_name_.find(key); it != symbols_by_name_.end()) {
            return it->second;
        }

        // Construct in place to avoid a move and the observer-registration update
        // that Symbol's move constructor requires.
        auto &symbol = symbols_.emplace_back();
        symbol.id_ = static_cast<symid_t>(symbols_.size() - 1);
        symbol.symbol_ = sym;
        symbol.venue_ = venue;
        symbols_by_name_.emplace(std::move(key), &symbol);
        return &symbol;
    }

    size_t size() const noexcept { return symbols_.size(); }

private:
    SymbolManager() = default;
    ~SymbolManager() = default;

    /// Keep Symbol addresses stable as instruments are added. The lookup map,
    /// exchanges and orders all retain Symbol pointers. A vector would invalidate
    /// them on reallocation, including when a 512-entry reserve is exceeded.
    std::deque<exch::Symbol> symbols_;
    std::unordered_map<SymbolKey, exch::Symbol*, SymbolKeyHash> symbols_by_name_;
};

}
