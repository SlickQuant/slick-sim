cmake_minimum_required(VERSION 3.25)

# Find Google Test (via vcpkg)
find_package(GTest CONFIG REQUIRED)

# Enable testing
enable_testing()

# Include GoogleTest module for gtest_discover_tests()
include(GoogleTest)

# ==============================================================================
# Unit Tests Executable
# ==============================================================================
set(SLICK_SIM_TEST_SOURCES
    # Priority 1: Matching Engine Tests
    unit/matching_engine/test_fifo_matching_engine.cpp
    unit/matching_engine/test_matching_engine_smp.cpp
    unit/matching_engine/test_matching_engine_tif.cpp
    unit/matching_engine/test_feed_liquidity_matching.cpp

    # Priority 2: Order Tests
    unit/common/test_order.cpp
    unit/common/test_order_conversions.cpp
    unit/common/test_fixed_string.cpp

    # Priority 3: Order Book Tests
    unit/order_book/test_orderbook_operations.cpp
    unit/order_book/test_orderbook_lookup.cpp

    # Priority 4: Symbol Tests
    unit/exchange/test_symbol.cpp
    unit/exchange/test_symbol_manager.cpp
    unit/exchange/test_order_pool_recycling.cpp
    unit/exchange/test_exchange_registry.cpp
    unit/exchange/test_exchange_lifecycle.cpp

    # Priority 5: Type & Message Tests
    unit/common/test_types.cpp
    unit/common/test_messages.cpp

    # Priority 6: Order Gateway Tests
    unit/order_gateway/test_order_store.cpp
    unit/order_gateway/test_pending_responses.cpp

    # Priority 7: Market Data Publisher Tests
    unit/market_data_publisher/test_sequenced_message.cpp
)

# One block per venue, gated on the TARGET rather than the option so a venue
# disabled for any other reason drops its suite too.
if(TARGET slick_sim_venue_coinbase)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/venues/coinbase/test_coinbase_exchange.cpp
        unit/venues/coinbase/test_self_match_prevention.cpp
        unit/venues/coinbase/test_coinbase_trade_encoder.cpp
    )
endif()

if(TARGET slick_sim_venue_hyperliquid)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/venues/hyperliquid/test_hyperliquid_exchange.cpp
        unit/venues/hyperliquid/test_hyperliquid_rest_client_manager.cpp
        unit/venues/hyperliquid/test_hyperliquid_info_proxy.cpp
        unit/venues/hyperliquid/test_hyperliquid_trade_encoder.cpp
        unit/venues/hyperliquid/test_hyperliquid_l2_diff.cpp
    )
endif()

add_executable(slick_sim_tests ${SLICK_SIM_TEST_SOURCES})

# slick::logger, slick::queue, slick::object-pool, slick::orderbook,
# nlohmann_json and the src/ include root all arrive via slick_sim_core, which
# `exchange` links PUBLIC.
target_link_libraries(slick_sim_tests
    PRIVATE
        matching_engine
        exchange
        order_gateway
        GTest::gtest
        GTest::gtest_main
)
# Same helper the executable uses, so the two binaries cannot end up with
# different venue sets. Each venue's PUBLIC dependencies come with it, which is how
# the Hyperliquid info-proxy suite gets slick::net::Http - a library that only the
# venues that declared it need, and that does not exist as a target at all in a
# venues-off build.
slick_sim_link_venues(slick_sim_tests)

target_include_directories(slick_sim_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# UBSan's alignment check is disabled for the test binary only.
#
# The wire structs are `#pragma pack(push, 1)`, so their 8-byte fields are
# misaligned by construction - OrderResponse::price lands at offset 404, and every
# price/qty/timestamp after it therefore sits at 4 or 5 mod 8. gtest's EXPECT_EQ
# takes its arguments by `const T&`, so *every* assertion that reads one of those
# fields binds a reference to a misaligned address, which UBSan reports as
# `reference binding to misaligned address ... which requires 8 byte alignment`.
#
# That is inherent to exercising a packed wire protocol with gtest, not a defect:
# there is no subset of assertions to correct, and the loads themselves are
# well-defined because the compiler knows the members are packed. Silencing it here
# rather than project-wide keeps the check active on src/, where a genuinely
# misaligned access would still be a real finding.
if(ENABLE_ASAN AND NOT MSVC)
  target_compile_options(slick_sim_tests PRIVATE -fno-sanitize=alignment)
endif()

# Set C++23 standard
set_target_properties(slick_sim_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        LINKER_LANGUAGE CXX
)

# Discover tests for CTest
# DISCOVERY_TIMEOUT: Debug builds with many DLL dependencies (OpenSSL, Boost, slick-net)
# can take several seconds to start on Windows. 30s gives enough headroom.
gtest_discover_tests(slick_sim_tests DISCOVERY_TIMEOUT 30)

# Add custom target for running tests
add_custom_target(run_tests
    COMMAND slick_sim_tests
    DEPENDS slick_sim_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running unit tests for slick-sim"
)
