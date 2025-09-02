cmake_minimum_required(VERSION 3.25)

set(BUILD_VERSION 0.1.0.0)
project(ExchangeSimulator VERSION ${BUILD_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
# Disable examples, tests, and benchmarks for slick_logger
set(BUILD_SLICK_LOGGER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SLICK_LOGGER_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SLICK_LOGGER_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    slick_logger
    GIT_REPOSITORY https://github.com/SlickTech/slick_logger.git
    GIT_TAG v0.1.0.1
)
FetchContent_MakeAvailable(slick_logger)

# Disable slick_socket example, and tests
set(BUILD_SLICK_SOCKET_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SLICK_SOCKET_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    slick_socket
    GIT_REPOSITORY https://github.com/SlickTech/slick_socket.git
    GIT_TAG main
)
FetchContent_MakeAvailable(slick_socket)

# Add QuickFIX dependency (prefer vcpkg installation)
find_package(quickfix CONFIG REQUIRED)
if(NOT quickfix_FOUND)
    # Try alternative package name
    find_package(QuickFIX CONFIG REQUIRED)
endif()

if(quickfix_FOUND)
    message(STATUS "Found quickfix")
elseif(QuickFIX_FOUND)
    message(STATUS "Found QuickFIX via alternative find_package")
else()
    message(FATAL_ERROR "quickfix not found.")
endif()

# Add nlohmann/json dependency
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

message(STATUS "**** ${slick_socket_INCLUDE_DIR} ${slick_logger_INCLUDE_DIR}")

message(STATUS "Slick Queue: ${slick_queue_SOURCE_DIR}")
message(STATUS "Slick Logger: ${slick_logger_SOURCE_DIR}")
message(STATUS "Slick Socket: ${slick_socket_SOURCE_DIR}")
message(STATUS "QuickFIX: ${quickfix_SOURCE_DIR} ${quickfix_LIBRARY}")

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
  if(MSVC)
    # MSVC AddressSanitizer (requires Visual Studio 2022 17.7+)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "19.37")
      add_compile_options(/fsanitize=address)
      add_link_options(/INCREMENTAL:NO)

      # Additional MSVC ASan flags for better compatibility
      add_compile_definitions(_DISABLE_VECTOR_ANNOTATION)
      add_compile_definitions(_DISABLE_STRING_ANNOTATION)

      message(STATUS "AddressSanitizer enabled for MSVC ${CMAKE_CXX_COMPILER_VERSION}")
    else()
      message(WARNING "AddressSanitizer requires Visual Studio 2022 17.7+ (current: ${CMAKE_CXX_COMPILER_VERSION})")
      set(ENABLE_ASAN OFF CACHE BOOL "ASan disabled - compiler too old" FORCE)
    endif()
  else()
    # GCC/Clang AddressSanitizer
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
endif()

add_subdirectory(src)

# Tests
# option(BUILD_TESTING "Build tests" ON)
# if(BUILD_TESTING)
#   find_package(GTest CONFIG REQUIRED)
#   enable_testing()
#   add_subdirectory(tests)
# endif()
