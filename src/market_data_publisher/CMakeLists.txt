add_library(market_data_publisher STATIC
    market_data_publisher.cpp
    publisher/coinbase_publisher.cpp
    publisher/hyperliquid_publisher.cpp
)

target_include_directories(market_data_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(market_data_publisher PRIVATE
    slick::logger slick::queue nlohmann_json::nlohmann_json
    slick::coinbase-advanced-cpp slick::orderbook
    hyperliquid unofficial::uwebsockets::uwebsockets
)