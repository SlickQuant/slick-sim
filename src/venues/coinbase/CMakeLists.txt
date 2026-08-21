# This adapter's dependencies, resolved in the adapter's own directory. The parent
# CMakeLists adds this directory only when SLICK_SIM_ENABLE_COINBASE is on, so
# nothing here re-tests the option and no shared file names the Coinbase SDK at all.
#
# GLOBAL because slick-sim and slick_sim_tests link this venue's object library from
# other directory scopes, and an imported target is otherwise visible only here and
# below - see slick_sim_find_venue_dependency() in cmake/SlickSimVenue.cmake.
find_package(coinbase-advanced-cpp 1.0.0 CONFIG QUIET GLOBAL)
if (NOT coinbase-advanced-cpp_FOUND)
    message(STATUS "Fetching coinbase-advanced-cpp...")
    # This was set in the top-level slick-orderbook block, which runs *after* the SDK
    # had been populated, so it never reached the SDK it names.
    set(BUILD_COINBASE_ADVANCED_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        coinbase-advanced-cpp
        GIT_REPOSITORY https://github.com/SlickQuant/coinbase-advanced-cpp.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(coinbase-advanced-cpp)
else()
    message(STATUS "Found coinbase-advanced-cpp ${coinbase-advanced-cpp_VERSION}: ${coinbase-advanced-cpp_DIR}")
endif()

# Both Coinbase order gateways call slick::net::Http directly, and both sign their
# requests with jwt-cpp. Neither is a core dependency - no other venue needs jwt-cpp
# at all - so both are resolved here. The lookups follow the SDK on purpose: it may
# already have brought either in as a build-tree target, which find_package cannot
# see, and the macro checks for exactly that.
slick_sim_find_venue_dependency(slick-net slick::net)
slick_sim_find_venue_dependency(jwt-cpp jwt-cpp::jwt-cpp)

# CoinbaseRestOrderGateway/CoinbaseWSOrderGateway derive from RestWsOrderGateway and
# CoinbasePublisher from WebsocketMarketDataPublisher. Both bases are uWebSockets-based
# and are compiled only for the venues that ask for them.
slick_sim_require_rest_ws()

slick_sim_add_venue(coinbase
    SOURCES
        coinbase_exchange.cpp
        coinbase_feed_callbacks.cpp
        coinbase_rest_order_gateway.cpp
        coinbase_ws_order_gateway.cpp
        coinbase_publisher.cpp
        coinbase_trade_encoder.cpp
        coinbase_venue.cpp
    DEPENDS
        slick::coinbase-advanced-cpp
        # Used directly by the two order gateways, and by nothing else in the
        # project. It has always ridden in on the Coinbase SDK's interface; naming
        # it stops that being luck.
        jwt-cpp::jwt-cpp
        # This adapter's networking stack, not the simulator's: slick::net::Http is
        # what the REST gateway sends orders with, and slick_sim_rest_ws carries the
        # uWebSockets bases its gateways and publisher serve from. A venue speaking a
        # binary session protocol - CME iLink, ICE - names slick::socket here instead
        # and pulls in neither.
        slick::net
        slick_sim_rest_ws
)
