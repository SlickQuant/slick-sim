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
# Call it from src/venues/<name>/CMakeLists.txt, beside that venue's SDK lookup,
# rather than from a condition shared by every venue: which networking stack an
# adapter speaks is the adapter's own business. The two REST/WebSocket venues here
# happen to share slick-net, but a CME iLink or ICE adapter speaks a binary session
# protocol over slick-socket and must not pull slick-net into the build - which is
# exactly what an "if any venue is enabled" condition does.
#
# GLOBAL is not optional. An imported target is visible only in the directory that
# created it and below, while slick-sim (src/) and slick_sim_tests (tests/) link the
# venue object library from sibling scopes and have to resolve every name in its
# interface. Without promotion, generate fails with "links to target slick::net but
# the target was not found".
#
# A no-op when the target already exists: a venue SDK resolved just above may have
# brought the same library in as a build-tree target, which find_package cannot see.
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
# The target is an OBJECT library on purpose. Each adapter registers itself with
# ExchangeRegistry from a namespace-scope initialiser, and nothing in the core ever
# names that symbol - out of a *static* library the linker drops the whole object
# as unreferenced and the venue silently vanishes from the registry (the same
# reason a GoogleTest suite compiled into a .lib runs zero tests). Object files are
# handed to the linker individually, so the initialiser survives, and it survives
# identically for slick-sim and slick_sim_tests with no per-consumer link flag for
# anyone to forget.
function(slick_sim_add_venue name)
    cmake_parse_arguments(SSV "" "" "SOURCES;DEPENDS" ${ARGN})
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
