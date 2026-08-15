add_library(market_data_publisher STATIC
    # market_data_publisher.cpp
    ws_md_publisher.cpp
    coinbase_publisher.cpp
    hyperliquid_publisher.cpp
    hyperliquid_l2_diff.cpp
    coinbase_trade_encoder.cpp
    hyperliquid_trade_encoder.cpp
)

target_include_directories(market_data_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(market_data_publisher PUBLIC
    slick::logger
    nlohmann_json::nlohmann_json
    slick::orderbook
    unofficial::uwebsockets::uwebsockets
    slick::coinbase-advanced-cpp
    hyperliquid 
)
