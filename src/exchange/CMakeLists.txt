add_library(exchange STATIC
    exchange.cpp
    exch_coinbase.cpp
    exch_coinbase_feed_callbacks.cpp
)

target_include_directories(exchange PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(exchange PRIVATE slick::queue slick::logger slick::object-pool matching_engine slick::orderbook Boost::uuid)
target_link_libraries(exchange PUBLIC slick::coinbase-advanced-cpp)