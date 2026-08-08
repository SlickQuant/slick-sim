add_library(exchange STATIC
    exchange.cpp
    exch_coinbase.cpp
    exch_coinbase_feed_callbacks.cpp
    exch_hyperliquid.cpp
    symbol.cpp
)

target_include_directories(exchange PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(exchange PUBLIC
    md_feed
    order_gateway
    market_data_publisher
    matching_engine
    Boost::uuid
)