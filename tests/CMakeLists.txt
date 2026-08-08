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
add_executable(slick_sim_tests
    # Priority 1: Matching Engine Tests
    unit/matching_engine/test_fifo_matching_engine.cpp
    unit/matching_engine/test_matching_engine_smp.cpp
    unit/matching_engine/test_matching_engine_tif.cpp
    unit/matching_engine/test_coinbase_market_data_matching.cpp

    # Priority 2: Order Tests
    unit/common/test_order.cpp
    unit/common/test_order_conversions.cpp

    # Priority 3: Order Book Tests
    unit/order_book/test_orderbook_operations.cpp
    unit/order_book/test_orderbook_lookup.cpp

    # Priority 4: Symbol Tests
    unit/exchange/test_symbol.cpp
    unit/exchange/test_symbol_manager.cpp
    unit/exchange/test_coinbase_exchange.cpp
    unit/exchange/test_hyperliquid_exchange.cpp

    # Priority 5: Type & Message Tests
    unit/common/test_types.cpp
    unit/common/test_messages.cpp
    unit/common/test_hyperliquid_info_proxy.cpp

    # Priority 6: Order Gateway Tests
    unit/order_gateway/test_hyperliquid_rest_client_manager.cpp

    # Priority 7: Market Data Publisher Tests
    unit/market_data_publisher/test_hyperliquid_l2_diff.cpp
)

target_link_libraries(slick_sim_tests
    PRIVATE
        matching_engine
        exchange
        order_gateway
        GTest::gtest
        GTest::gtest_main
        slick::logger
        slick::queue
        slick::object-pool
        slick::orderbook
        slick::net
        nlohmann_json::nlohmann_json
)

target_include_directories(slick_sim_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

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
