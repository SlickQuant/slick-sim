#include "matching_config.hpp"
#include "fifo_matching_engine.hpp"
#include "pro_rata_matching_engine.hpp"
#include <slick/logger.hpp>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>

using namespace slick::sim;
using namespace slick::sim::engine;

namespace {

// Both readers check the JSON value before narrowing it. get<int>() turns 100.5
// into 100 and 2^32 into 0, and a double too big for qty_t has no defined cast.

uint8_t read_percent(const nlohmann::json &params, const char *key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return 0;
    }
    if (it->is_number_integer()) {   // floats and bools are not
        const auto v = it->get<int64_t>();   // an unsigned above INT64_MAX wraps negative
        if (v >= 0 && v <= 100) {
            return static_cast<uint8_t>(v);
        }
    }
    throw std::invalid_argument(std::format("pro_rata.{} must be an integer 0-100, got {}", key, it->dump()));
}

qty_t read_qty(const nlohmann::json &params, const char *key, double fallback) {
    constexpr double kMax = static_cast<double>(std::numeric_limits<qty_t>::max() / DOUBLE_MULTIPLIER);
    const auto it = params.find(key);
    if (it == params.end()) {
        return to_qty_t(fallback);
    }
    if (it->is_number() && std::isfinite(it->get<double>()) && std::fabs(it->get<double>()) <= kMax) {
        return to_qty_t(it->get<double>());
    }
    throw std::invalid_argument(std::format("pro_rata.{} must be a number within +/-{}, got {}", key, kMax, it->dump()));
}

}   // namespace

void ProRataConfig::validate() const {
    if (lot_size <= 0) {
        throw std::invalid_argument(std::format("pro_rata.lot_size must be positive, got {}", to_qty_double(lot_size)));
    }
    if (min_allocation < 0) {
        throw std::invalid_argument(std::format("pro_rata.min_allocation must not be negative, got {}",
                                                to_qty_double(min_allocation)));
    }
    if (fifo_percent > 100) {
        throw std::invalid_argument(std::format("pro_rata.fifo_percent must be 0-100, got {}", fifo_percent));
    }
}

MatchingConfig MatchingConfig::from_json(const nlohmann::json &config) {
    MatchingConfig out;
    try {
        const auto algorithm = config.value("matching_algorithm", std::string("fifo"));
        if (algorithm == "fifo") {
            if (config.contains("pro_rata")) {
                LOG_WARN("pro_rata settings ignored: matching_algorithm is fifo");
            }
            return out;
        }
        if (algorithm != "pro_rata") {
            throw std::invalid_argument(std::format(
                "unknown matching_algorithm '{}'; expected fifo or pro_rata", algorithm));
        }

        out.type = MatchingEngine::Type::ProRata;
        const auto params = config.value("pro_rata", nlohmann::json::object());
        if (!params.is_object()) {   // find() on a non-object finds nothing, which would mean defaults
            throw std::invalid_argument(std::format("pro_rata must be an object, got {}", params.dump()));
        }
        out.pro_rata.lot_size = read_qty(params, "lot_size", 1.0);
        out.pro_rata.min_allocation = read_qty(params, "min_allocation", 0.0);
        out.pro_rata.fifo_percent = read_percent(params, "fifo_percent");
    } catch (const nlohmann::json::exception &e) {
        throw std::invalid_argument(std::format("bad matching config: {}", e.what()));
    }
    out.pro_rata.validate();
    return out;
}

std::unique_ptr<MatchingEngine> slick::sim::engine::createMatchingEngine(const MatchingConfig &config,
                                                                         slick::queue<OrderResponse> &response_queue) {
    if (config.type == MatchingEngine::Type::ProRata) {
        return std::make_unique<ProRataMatchingEngine>(response_queue, config.pro_rata);
    }
    return std::make_unique<FifoMatchingEngine>(response_queue);
}
