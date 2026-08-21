add_library(exchange STATIC
    exchange.cpp
    exchange_registry.cpp
    symbol.cpp
)

target_include_directories(exchange PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(exchange PUBLIC
    slick_sim_core
    md_feed
    order_gateway
    market_data_publisher
    matching_engine
    Boost::uuid
)