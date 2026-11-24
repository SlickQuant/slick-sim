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
    unit/matching_engine/test_pro_rata_matching_engine.cpp
    unit/matching_engine/test_matching_config.cpp

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
    unit/exchange/test_order_updates_and_tags.cpp
    unit/exchange/test_market_mirror.cpp
    unit/exchange/test_instance_expansion.cpp

    # Priority 5: Type & Message Tests
    unit/common/test_types.cpp
    unit/common/test_messages.cpp
    unit/common/test_shm_cleanup.cpp
    unit/common/test_timestamp.cpp
    unit/common/test_decimal.cpp
    unit/common/test_math.cpp

    # Priority 6: Order Gateway Tests
    unit/order_gateway/test_order_store.cpp
    unit/order_gateway/test_pending_responses.cpp
    unit/order_gateway/test_retransmit_ring.cpp
    unit/order_gateway/test_cl_ord_id_index.cpp

    # Priority 7: Market Data Publisher Tests
    unit/market_data_publisher/test_sequenced_message.cpp

    # Priority 8: CLI endpoints (header-only)
    unit/cli/test_cli_endpoint.cpp
)

# One block per venue, gated on the TARGET rather than the option so a venue
# disabled for any other reason drops its suite too.
# The multicast feed plumbing is header-only over slick-socket, which only the TCP
# gateway build brings in.
if(SLICK_SIM_ENABLE_TCP_GATEWAY)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/md_feed/test_multicast_packet_feed.cpp
    )
endif()

if(TARGET slick_sim_cli)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/cli/test_cli_server.cpp
    )
endif()

if(TARGET slick_sim_pcap)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/md_feed/test_pcap_reader.cpp
        unit/md_feed/test_pcap_replay_source.cpp
    )
endif()

if(TARGET slick_sim_pcap_feed)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/md_feed/test_pcap_feed.cpp
    )
endif()

if(TARGET slick_sim_venue_coinbase)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/venues/coinbase/test_coinbase_exchange.cpp
        unit/venues/coinbase/test_self_match_prevention.cpp
        unit/venues/coinbase/test_coinbase_trade_encoder.cpp
        unit/venues/coinbase/test_coinbase_routing.cpp
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

# Suites a venue declared through slick_sim_add_venue(... TESTS ...), chiefly
# out-of-tree venues. They include the shared fixtures as "unit/test_helpers.hpp".
get_property(_slick_sim_venue_tests GLOBAL PROPERTY SLICK_SIM_VENUE_TESTS)
list(APPEND SLICK_SIM_TEST_SOURCES ${_slick_sim_venue_tests})

# End-to-end suites spawn slick-sim and drive its ports from a separate executable. The Coinbase
# suite needs the Coinbase venue and the CLI: the harness waits for the CLI port, stops the
# simulator with `sim shutdown`, and tests the CLI itself. A build with the TCP gateway off has no CLI.
if(TARGET slick_sim_venue_coinbase AND SLICK_SIM_BUILD_INTEGRATION_TESTS)
    if(TARGET slick_sim_cli)
        add_subdirectory(integration)
    else()
        message(STATUS "coinbase_integration skipped: it needs the CLI (SLICK_SIM_ENABLE_TCP_GATEWAY)")
    endif()
endif()

# Add out-of-process suites declared by out-of-tree venues through
# slick_sim_add_venue(... INTEGRATION_TESTS ...). Each directory defines its
# executable and add_test() entries, as tests/integration/ does for Coinbase.
if(SLICK_SIM_BUILD_INTEGRATION_TESTS)
    get_property(_slick_sim_venue_integration_dirs GLOBAL PROPERTY SLICK_SIM_VENUE_INTEGRATION_TESTS)
    foreach(_dir IN LISTS _slick_sim_venue_integration_dirs)
        # Use the path hash to give each suite a distinct binary directory.
        string(SHA1 _key "${_dir}")
        add_subdirectory("${_dir}" "${CMAKE_CURRENT_BINARY_DIR}/venue-integration-${_key}")
    endforeach()
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
if(TARGET slick_sim_pcap)
    target_link_libraries(slick_sim_tests PRIVATE slick_sim_pcap)
endif()
if(TARGET slick_sim_pcap_feed)
    target_link_libraries(slick_sim_tests PRIVATE slick_sim_pcap_feed)
endif()
if(TARGET slick_sim_cli)
    target_link_libraries(slick_sim_tests PRIVATE slick_sim_cli)
endif()
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
  # OrderStore is header-only, so its apply() is compiled into this file. Keep the check on here:
  # the file's assertions read OrderStore's own unpacked Order, and the check caught apply()
  # binding a reference to a packed OrderResponse field. Source options come after target ones.
  set_source_files_properties(unit/order_gateway/test_order_store.cpp
      PROPERTIES COMPILE_OPTIONS -fsanitize=alignment)
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
