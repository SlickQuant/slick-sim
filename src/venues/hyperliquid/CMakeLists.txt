# As for Coinbase: this adapter's own dependencies, resolved here because the parent
# CMakeLists adds this directory only when SLICK_SIM_ENABLE_HYPERLIQUID is on. GLOBAL
# so the imported targets survive into src/ and tests/, which link this venue's
# object library from their own directory scopes.
find_package(hyperliquid 0.3.0 CONFIG QUIET GLOBAL)
if (NOT hyperliquid_FOUND)
    message(STATUS "Fetching hyperliquid-cpp...")
    FetchContent_Declare(
        hyperliquid-cpp
        GIT_REPOSITORY https://github.com/SlickQuant/hyperliquid-cpp.git
        GIT_TAG v0.3.0
    )
    FetchContent_MakeAvailable(hyperliquid-cpp)
else()
    message(STATUS "Found hyperliquid ${hyperliquid_VERSION}: ${hyperliquid_DIR}")
endif()

# The REST order gateway and the info proxy call slick::net::Http. A no-op when the
# SDK above already brought slick-net in as a build-tree target.
slick_sim_find_venue_dependency(slick-net slick::net)

# HyperliquidRestOrderGateway derives from RestWsOrderGateway and HyperliquidPublisher
# from WebsocketMarketDataPublisher - the uWebSockets bases, built on demand.
slick_sim_require_rest_ws()

slick_sim_add_venue(hyperliquid
    SOURCES
        hyperliquid_exchange.cpp
        hyperliquid_live_ws_feed.cpp
        hyperliquid_rest_order_gateway.cpp
        hyperliquid_publisher.cpp
        hyperliquid_trade_encoder.cpp
        hyperliquid_l2_diff.cpp
        hyperliquid_venue.cpp
    DEPENDS
        hyperliquid
        # As for Coinbase: slick::net::Http is this adapter's own networking stack,
        # used by the REST order gateway and the info proxy, and slick_sim_rest_ws
        # carries the uWebSockets bases. Declared per venue so a future venue on a
        # different stack inherits neither.
        slick::net
        slick_sim_rest_ws
)
