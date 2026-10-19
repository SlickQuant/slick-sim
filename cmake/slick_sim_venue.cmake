# Helpers for declaring optional venue adapters.
#
# A venue adapter is one directory under src/venues/ holding everything that venue
# needs - its Exchange subclass, market-data feed, order gateways and publisher -
# and one call to slick_sim_add_venue() below. Nothing outside that directory and
# the SLICK_SIM_VENUE_LIST entry in src/common/types.hpp has to change to add one.

# Captured at include time so the helpers below do not read CMAKE_SOURCE_DIR, which
# points at the outer project if slick-sim is ever added as a subproject.
get_filename_component(SLICK_SIM_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../src" ABSOLUTE)

# Resolves an external package that some venue adapters need and the core does not.
#
#   slick_sim_find_venue_dependency(slick-net slick::net)
#
# Call from the venue's CMakeLists.txt beside its SDK lookup. Each adapter
# chooses its own networking stack: the REST/WebSocket venues use slick-net;
# a binary CME iLink or ICE adapter uses slick-socket. Testing whether any venue
# is enabled would incorrectly pull slick-net into those builds.
#
# GLOBAL makes imported targets visible to slick-sim and slick_sim_tests in
# sibling directories. Without it, generation fails with "links to target
# slick::net but the target was not found".
#
# If the target already exists, do nothing. A venue SDK may have created it as
# a build-tree target, which find_package cannot see.
macro(slick_sim_find_venue_dependency pkg tgt)
    if (NOT TARGET ${tgt})
        find_package(${pkg} CONFIG REQUIRED GLOBAL ${ARGN})
        message(STATUS "Found ${pkg} ${${pkg}_VERSION}: ${${pkg}_DIR}")
    endif()
endmacro()

# Creates the shared REST/WebSocket support target on first call, and resolves
# uWebSockets with it.
#
#   slick_sim_require_rest_ws()   # then name slick_sim_rest_ws in DEPENDS
#
# RestWsOrderGateway and WebsocketMarketDataPublisher are the only uWebSockets in the
# project, and only a venue that serves HTTP/WebSocket derives from them - so they are
# not core. A build with no such adapter (core-only, or a future CME/ICE venue over
# slick::socket) neither compiles them nor needs uWebSockets installed at all, which
# is what makes the venues-off configuration a real assertion rather than a formality.
#
# The two sources stay in src/order_gateway/ and src/market_data_publisher/ beside
# the bases they implement and the headers that declare them: it is the target that
# is optional, not the directory. Real targets are global once created, so the second
# venue to call this reuses the first one's.
function(slick_sim_require_rest_ws)
    if(TARGET slick_sim_rest_ws)
        return()
    endif()

    slick_sim_find_venue_dependency(unofficial-uwebsockets unofficial::uwebsockets::uwebsockets)

    add_library(slick_sim_rest_ws STATIC
        ${SLICK_SIM_SRC_DIR}/order_gateway/rest_ws_order_gateway.cpp
        ${SLICK_SIM_SRC_DIR}/market_data_publisher/ws_md_publisher.cpp
    )
    # PUBLIC: the venue headers that derive from these bases include the uWebSockets
    # headers themselves, so a venue linking this target has to see them too.
    target_link_libraries(slick_sim_rest_ws PUBLIC
        order_gateway
        market_data_publisher
        unofficial::uwebsockets::uwebsockets
    )
endfunction()

# Declares one optional venue adapter.
#
#   slick_sim_add_venue(coinbase
#       SOURCES coinbase_exchange.cpp coinbase_publisher.cpp ...
#       DEPENDS slick::coinbase-advanced-cpp slick::net)
#
# DEPENDS is where a venue names everything it links that the core does not,
# including its networking stack - slick::net for an HTTP/WebSocket venue,
# slick::socket for a binary session one. Linking is PUBLIC, so those propagate to
# whatever links the venue (slick-sim and slick_sim_tests) without either of them
# naming a venue library.
#
# Use an OBJECT library to retain the namespace-scope registry initialiser.
# Nothing in the core references it by name, so a static library could lose
# the whole object as unreferenced, just as a GoogleTest suite in a .lib can
# run zero tests. Passing object files directly preserves registration in both
# slick-sim and slick_sim_tests without per-consumer linker flags.
#
# TESTS lists GoogleTest sources to compile into slick_sim_tests. This lets
# out-of-tree venues bring tests through SLICK_SIM_EXTERNAL_VENUES without
# editing tests/CMakeLists.txt. Relative paths use the calling directory.
function(slick_sim_add_venue name)
    cmake_parse_arguments(SSV "" "" "SOURCES;DEPENDS;TESTS" ${ARGN})
    set(target slick_sim_venue_${name})

    add_library(${target} OBJECT ${SSV_SOURCES})

    # `exchange` links slick_sim_core, md_feed, order_gateway,
    # market_data_publisher and matching_engine PUBLIC, so this one edge carries
    # every core header a venue needs. Venue-internal headers are quoted and
    # resolve from the venue's own directory.
    target_link_libraries(${target} PUBLIC exchange ${SSV_DEPENDS})

    # Lets a consumer - chiefly the test binary - compile a venue's suite only when
    # that venue is actually built, without re-deriving the option.
    string(TOUPPER ${name} name_uc)
    target_compile_definitions(${target} INTERFACE SLICK_SIM_HAS_${name_uc}=1)

    set_property(GLOBAL APPEND PROPERTY SLICK_SIM_ENABLED_VENUES ${name})
    # The union of every enabled venue's DEPENDS, so slick_sim_link_venues() can
    # tell which networking libraries are actually in the binary. Keeping that fact
    # in DEPENDS alone means a venue declares its stack once.
    set_property(GLOBAL APPEND PROPERTY SLICK_SIM_VENUE_DEPENDS ${SSV_DEPENDS})

    foreach(test_source IN LISTS SSV_TESTS)
        cmake_path(ABSOLUTE_PATH test_source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set_property(GLOBAL APPEND PROPERTY SLICK_SIM_VENUE_TESTS "${test_source}")
    endforeach()
endfunction()

# Links every enabled venue into `target`. A function rather than two open-coded
# call sites so the executable and the test binary cannot drift apart.
function(slick_sim_link_venues target)
    get_property(venues GLOBAL PROPERTY SLICK_SIM_ENABLED_VENUES)
    foreach(venue IN LISTS venues)
        target_link_libraries(${target} PRIVATE slick_sim_venue_${venue})
    endforeach()

    # Nothing venue-specific is linked here: each adapter's PUBLIC DEPENDS carries
    # its own networking library through. What remains is main.cpp's log bridge,
    # which routes slick::net's output into slick-logger and must not name a venue,
    # so it is gated on that library actually being present rather than on "any
    # venue at all" - a CME or ICE adapter over slick::socket would satisfy the
    # latter while having no slick::net to bridge.
    get_property(venue_depends GLOBAL PROPERTY SLICK_SIM_VENUE_DEPENDS)
    if("slick::net" IN_LIST venue_depends)
        target_compile_definitions(${target} PRIVATE SLICK_SIM_HAS_SLICK_NET=1)
    endif()
endfunction()
