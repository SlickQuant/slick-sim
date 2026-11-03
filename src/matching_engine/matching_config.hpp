#pragma once

#include "matching_engine.hpp"
#include <common/types.hpp>
#include <nlohmann/json.hpp>
#include <memory>

namespace slick::sim::engine {

/// Pro-rata allocation rules. Quantities use the venue's fixed-point scale.
struct ProRataConfig {
    qty_t lot_size = DOUBLE_MULTIPLIER;   ///< allocations round down to whole lots
    qty_t min_allocation = 0;             ///< a pro-rata share below this rounds to zero
    uint8_t fifo_percent = 0;             ///< share of the incoming quantity filled FIFO before pro-rata

    /// Throws std::invalid_argument unless lot_size > 0, min_allocation >= 0 and
    /// fifo_percent <= 100. A zero lot divides by zero, and a percentage over 100
    /// allocates more than the aggressor has.
    void validate() const;

    bool operator==(const ProRataConfig &) const = default;
};

/// Which engine a venue's symbols match with.
struct MatchingConfig {
    MatchingEngine::Type type = MatchingEngine::Type::FIFO;
    ProRataConfig pro_rata;

    /// Reads `matching_algorithm` ("fifo" by default, or "pro_rata") and, for
    /// pro-rata, the `pro_rata` block: `lot_size` and `min_allocation` in venue
    /// units, `fifo_percent` 0-100. Throws std::invalid_argument on anything else,
    /// a wrong JSON type included, since falling back to FIFO would silently change
    /// who gets filled.
    static MatchingConfig from_json(const nlohmann::json &config);
};

std::unique_ptr<MatchingEngine> createMatchingEngine(const MatchingConfig &config,
                                                     slick::queue<OrderResponse> &response_queue);

}   // namespace slick::sim::engine
