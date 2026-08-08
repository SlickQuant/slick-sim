add_library(md_feed STATIC
    hyperliquid_live_ws_feed.cpp
)

target_include_directories(md_feed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(md_feed PUBLIC
    slick::logger
    hyperliquid
)