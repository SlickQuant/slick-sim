add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(engine INTERFACE slick_logger)

# Order Gateway library
add_library(order_gateway
    order_gateway/order_gateway.cpp
    order_gateway/order_gateway.h
    order_gateway/order.h
    order_gateway/message_parser.cpp
    order_gateway/message_parser.h
    order_gateway/fix_parser.cpp
    order_gateway/fix_parser.h
    order_gateway/sbe_parser.cpp
    order_gateway/sbe_parser.h
    order_gateway/json_parser.cpp
    order_gateway/json_parser.h
)

target_include_directories(order_gateway 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/order_gateway
           ${CMAKE_CURRENT_SOURCE_DIR}/order_gateway/sbe 
)

# Link libraries - handle both vcpkg and FetchContent QuickFIX
target_link_libraries(order_gateway 
    PUBLIC slick_socket slick_logger slick_queue nlohmann_json::nlohmann_json quickfix
)

add_executable(exchange_simulator sim/main.cpp)
target_link_libraries(exchange_simulator PRIVATE engine order_gateway)
target_include_directories(exchange_simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
