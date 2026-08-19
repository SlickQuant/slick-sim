#pragma once

#include <deque>
#include <string_view>
#include <unordered_map>
#include <exchange/symbol.hpp>
#include <common/market_data.hpp>
#include "types.hpp"

namespace slick::sim {

/// Identity of an instrument: a name is only unique within a venue.
///
/// Keying the lookup on the name alone let two venues listing the same ticker
/// collide - the second `createSymbol` found the name already present, so its
/// Symbol was built but never became reachable, and every lookup for it returned
/// the *other* venue's instrument instead.
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
        // The key holds its name inline, so this costs no allocation. Keyed by
        // std::string it built - and freed - a string on every lookup, including
        // one per inbound order on the request path.
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

        // Constructed in place rather than built on the stack and moved in: the
        // move is pure overhead here, and Symbol owns an observer registration
        // that the move constructor has to re-point.
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

    /// A deque, not a vector, because everything here hands out Symbol* and keeps
    /// them: the map below, the exchanges, and every Order that resolves to an
    /// instrument. A vector reserved for 512 symbols left all of those dangling
    /// the moment the 513th was created - the map entries, the pointers callers
    /// had already stored, and the return value of the create that reallocated.
    /// deque never moves an element that is already in it.
    std::deque<exch::Symbol> symbols_;
    std::unordered_map<SymbolKey, exch::Symbol*, SymbolKeyHash> symbols_by_name_;
};

}
