#pragma once

#include <cstdint>

namespace slick::sim::exch {

/// The market-data channels the Hyperliquid adapter serves.
///
/// Split out of hyperliquid_exchange.hpp so HyperliquidPublisher can name it without
/// including the exchange: the publisher needs nothing but this enum, and pulling
/// in the exchange header dragged HyperliquidExchange, <hyperliquid/info.hpp> and
/// <hyperliquid/utils/l2_diff.hpp> into every translation unit that touched the
/// publisher - an inverted dependency, since the exchange owns the publisher.
enum class HyperliquidChannel : uint8_t {
    L2_BOOK = 0,
    TRADES  = 1,
    L2      = 2,
    __COUNT__ = 3,
};

}   // end namespace slick::sim::exch
