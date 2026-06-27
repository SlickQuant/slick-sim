add_library(order_gateway
    rest_ws_order_gateway.cpp
    tcp_order_gateway.cpp
    coinbase_rest_order_gateway.cpp
    coinbase_ws_order_gateway.cpp
    hyperliquid_rest_order_gateway.cpp
    message_parser.cpp
    fix_parser.cpp
    sbe_parser.cpp
    json_parser.cpp
)

target_include_directories(order_gateway 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/..
           ${CMAKE_CURRENT_SOURCE_DIR}/sbe
)

# Link libraries - handle both vcpkg and FetchContent QuickFIX
target_link_libraries(order_gateway PUBLIC
    slick::socket
    slick::logger
    nlohmann_json::nlohmann_json
    unofficial::uwebsockets::uwebsockets
    quickfix
    slick::coinbase-advanced-cpp
    hyperliquid
)
