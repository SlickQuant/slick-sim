add_library(core INTERFACE)
target_include_directories(core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(core INTERFACE spdlog::spdlog)

add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine)

add_executable(exchange_simulator sim/main.cpp)
target_link_libraries(exchange_simulator PRIVATE core engine)
