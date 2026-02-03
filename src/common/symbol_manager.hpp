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

    exch::Symbol* addSymbol(exch::Symbol&& sym) {
        auto *symbol = getSymbol(sym.symbol_);
        if (!symbol) {
            symbols_.emplace_back(std::move(sym));
            auto &symbol = symbols_.back();
            symbol.setId(symbols_.size() - 1);
            symbols_by_name_.emplace(symbol.symbol_, &symbol);
            return &symbol;
        }
        return symbol;
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