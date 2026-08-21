add_library(matching_engine STATIC
    matching_engine.cpp
    fifo_matching_engine.cpp
)

target_include_directories(matching_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(matching_engine PUBLIC
    slick_sim_core
)