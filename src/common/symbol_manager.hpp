#pragma once

#include <string>
#include <exchange/symbol.hpp>
#include "types.hpp"
#include <unordered_map>

namespace slick::sim {

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

    exch::Symbol* getSymbol(std::string_view sym) const {
        auto it = symbols_by_name_.find(std::string(sym));
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

    exch::Symbol* createSymbol(std::string_view sym) {
        exch::Symbol symbol;
        symbol.id_ = symbols_.size();
        symbol.symbol_ = std::string(sym);
        symbols_.emplace_back(std::move(symbol));
        symbols_by_name_.emplace(symbol.symbol_, &symbols_.back());
        return &symbols_.back();
    }

private:
    SymbolManager() {
        symbols_.reserve(512);
    }
    ~SymbolManager() = default;

    std::vector<exch::Symbol> symbols_;
    std::unordered_map<std::string, exch::Symbol*> symbols_by_name_;
};

}