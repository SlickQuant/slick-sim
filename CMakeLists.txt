cmake_minimum_required(VERSION 3.25)

set(BUILD_VERSION 0.1.0)
project(ExchangeSimulator VERSION ${BUILD_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions(-DNDEBUG)
endif()

include(FetchContent)

find_package(slick-logger 1.0.4 CONFIG QUIET)
if (NOT slick-logger_FOUND)
    message(STATUS "Fetching slick-logger...")
    # Disable examples, tests, and benchmarks for slick_logger
    set(BUILD_SLICK_LOGGER_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_LOGGER_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_LOGGER_BENCHMARKS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-logger
        GIT_REPOSITORY https://github.com/SlickQuant/slick-logger.git
        GIT_TAG v1.0.4
    )
    FetchContent_MakeAvailable(slick-logger)
endif()

find_package(slick-socket 1.0.4 CONFIG QUIET)
if (NOT slick-socket_FOUND)
    message(STATUS "Fetching slick-socket...")
    # Disable slick-socket example, and tests
    set(BUILD_SLICK_SOCKET_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_SOCKET_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_SOCKET_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-socket
        GIT_REPOSITORY https://github.com/SlickQuant/slick-socket.git
        GIT_TAG v1.0.4
    )
    FetchContent_MakeAvailable(slick-socket)
else()
    message(STATUS "Found slick-socket: ${slick-socket_VERSION}")
endif()

find_package(slick-object-pool 0.1.3 CONFIG QUIET)
if (NOT slick-object-pool_FOUND)
    message(STATUS "Fetching slick-object-pool...")
    set(BUILD_SLICK_OBJECTPOOL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-object-pool
        GIT_REPOSITORY https://github.com/SlickQuant/slick-object-pool.git
        GIT_TAG v0.1.3
    )
    FetchContent_MakeAvailable(slick-object-pool)
else()
    message(STATUS "Found slick-object-pool: ${slick-object-pool_VERSION}")
endif()

find_package(coinbase-advanced-cpp 0.1.0 CONFIG QUIET)
if (NOT coinbase-advanced-cpp_FOUND)
    message(STATUS "Fetching coinbase-advanced-cpp...")
    set(BUILD_COINBASE_ADVANCED_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        coinbase-advanced-cpp
        GIT_REPOSITORY https://github.com/SlickQuant/coinbase-advanced-cpp.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(coinbase-advanced-cpp)
else()
    message(STATUS "Found coinbase-advanced-cpp: ${coinbase-advanced-cpp_VERSION}")
endif()

find_package(slick-net 1.2.2 CONFIG QUIET)
if (NOT slick-net_FOUND)
    message(STATUS "Fetching slick-net...")
    set(BUILD_SLICK_NET_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_NET_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-net
        GIT_REPOSITORY https://github.com/SlickQuant/slick-net.git
        GIT_TAG v1.2.4
    )
    FetchContent_MakeAvailable(slick-net)
else()
    message(STATUS "Found slick-net: ${slick-net_VERSION}")
endif()

find_package(slick-orderbook CONFIG QUIET)
if (NOT slick-orderbook_FOUND)
    message(STATUS "Fetching slick-orderbook...")
    set(BUILD_COINBASE_ADVANCED_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-orderbook
        GIT_REPOSITORY https://github.com/SlickQuant/slick-orderbook.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(slick-orderbook)
else()
    message(STATUS "Found slick-orderbook: ${slick-orderbook_VERSION}")
endif()

# Add QuickFIX dependency (prefer vcpkg installation)
find_package(quickfix CONFIG REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(foonathan_memory CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS)

if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_compile_options(
        /W4
        /wd4324  # Suppress C4324: structure was padded due to alignment specifier (intentional for cache alignment)
        /wd4200 
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>  # Whole program optimization
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>  # Link-time code generation (required with /GL)
    )
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-flto>
    )
endif()

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
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")
  endif()
endif()

add_subdirectory(src)

# Tests
# option(BUILD_EXCH_SIMULATOR_TESTING "Build tests" ON)
# if(BUILD_EXCH_SIMULATOR_TESTING)
#   find_package(GTest CONFIG REQUIRED)
#   enable_testing()
#   add_subdirectory(tests)
# endif()
