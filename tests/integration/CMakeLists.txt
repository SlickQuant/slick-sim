# End-to-end tests that drive the real `slick-sim` executable over its ports.
#
# The clients are separate processes running the real coinbase-advanced-cpp SDK:
# the SDK caches COINBASE_API_KEY / COINBASE_API_SECRET in function-static
# locals, and the simulator routes execution reports by the JWT `sub`, so three
# trading users need three processes to be three distinct clients.

add_executable(slick_sim_coinbase_client coinbase_client.cpp)
target_link_libraries(slick_sim_coinbase_client PRIVATE
    slick::coinbase-advanced-cpp
    slick::net
    nlohmann_json::nlohmann_json
)
set_target_properties(slick_sim_coinbase_client PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# The suite handles process control, config generation, the CLI protocol and raw HTTP probes. The
# SDK clients run in separate executables as described above.
add_executable(slick_sim_integration_tests
    main.cpp
    test_environment.cpp
    coinbase_integration_test.cpp
    coinbase_lifecycle_test.cpp
    harness/process.cpp
    harness/sim_harness.cpp
    harness/client_driver.cpp
    harness/rest_probe.cpp
)
# slick::sim_core carries src/ as an include root and the header-only core types
# (common/control_messages.hpp is parsed straight out of the tree).
target_link_libraries(slick_sim_integration_tests PRIVATE
    slick::sim_core
    slick::net
    GTest::gtest
    nlohmann_json::nlohmann_json
)
target_include_directories(slick_sim_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(slick_sim_integration_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
# `ctest` does not build; make the test target carry the two executables it spawns.
add_dependencies(slick_sim_integration_tests slick-sim slick_sim_coinbase_client)

add_test(NAME coinbase_integration
    COMMAND slick_sim_integration_tests
        --sim $<TARGET_FILE:slick-sim>
        --client $<TARGET_FILE:slick_sim_coinbase_client>
        --config ${PROJECT_SOURCE_DIR}/config/coinbase_self_contained.json
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
# Each run chooses its ports, and tests needing isolation start their own simulator. Two copies of
# the suite would share the three clients and build directory, so run the suite serially in one
# process. The shared simulator starts once.
set_tests_properties(coinbase_integration PROPERTIES
    TIMEOUT 900
    RUN_SERIAL TRUE
    LABELS integration
)
