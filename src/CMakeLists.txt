add_subdirectory(md_feed)
add_subdirectory(exchange)
add_subdirectory(matching_engine)
add_subdirectory(order_gateway)
add_subdirectory(market_data_publisher)

add_executable(slick-sim sim/main.cpp)
target_link_libraries(slick-sim PRIVATE exchange)
target_include_directories(slick-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
