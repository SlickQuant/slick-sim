# Everything under src/ is reached as <component/header.hpp>, and the header-only
# layers (common/, utils/, order_book/) have no target of their own. Carrying that
# include root plus the header-only dependencies on one INTERFACE target replaces
# order_gateway's PUBLIC include directories, which every other target was
# silently relying on to resolve <common/...> at all.
#
# Boost::multiprecision is here because common/types.hpp includes
# <boost/multiprecision/cpp_int.hpp> for cum_value_t and nothing has ever linked
# it - it compiled only because vcpkg puts Boost on the default include path.
add_library(slick_sim_core INTERFACE)
add_library(slick::sim_core ALIAS slick_sim_core)
target_include_directories(slick_sim_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(slick_sim_core INTERFACE
    slick::logger
    slick::queue
    slick::object-pool
    slick::orderbook
    nlohmann_json::nlohmann_json
    Boost::multiprecision
)

add_subdirectory(md_feed)
add_subdirectory(exchange)
add_subdirectory(matching_engine)
add_subdirectory(order_gateway)
add_subdirectory(market_data_publisher)
# After exchange/: every venue target links it.
add_subdirectory(venues)

add_executable(slick-sim sim/main.cpp)
target_link_libraries(slick-sim PRIVATE exchange)
# Adds the enabled venue objects, and with them whatever networking stack each one
# declared - plus SLICK_SIM_HAS_SLICK_NET for main.cpp's log bridge, but only if
# some enabled venue actually speaks slick::net.
slick_sim_link_venues(slick-sim)
