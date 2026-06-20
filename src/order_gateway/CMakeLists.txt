add_library(order_gateway
    client_manager/coinbase_rest_client_manager.cpp
    client_manager/coinbase_ws_client_manager.cpp
    client_manager/hyperliquid_rest_client_manager.cpp
    order_gateway.cpp
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
target_link_libraries(order_gateway 
    PUBLIC slick::net slick::socket slick::logger slick::queue slick::coinbase-advanced-cpp nlohmann_json::nlohmann_json quickfix unofficial::uwebsockets::uwebsockets hyperliquid
)