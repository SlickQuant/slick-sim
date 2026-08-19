cmake_minimum_required(VERSION 3.25)

project(ExchangeSimulator VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(BUILD_EXCH_SIMULATOR_TESTING "Build tests" ON)

# -march=native bakes the build machine's instruction set into the binary.
#
# That is wrong for anything CI builds, because ci.yml uploads the Release
# binaries and release.yml attaches them to GitHub Releases: a runner with
# AVX-512 produces a binary that dies with SIGILL on any older user CPU.
#
# It is also unreliable on cloud VMs even when build and run happen on the same
# machine — the hypervisor can mask features off via XCR0 that the compiler's
# native detection still sees in CPUID, so the binary faults on instructions
# the CPU advertised but will not execute.
#
# Off by default so distributed binaries are portable. Turn it on for local or
# self-hosted production builds, where the target CPU is the build CPU:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_NATIVE_ARCH=ON
option(ENABLE_NATIVE_ARCH "Optimise for the build machine's CPU (-march=native)" OFF)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions(-DNDEBUG)
endif()

# Must precede every find_package/FetchContent call below.
#
# slick-shm's exported target declares Threads::Threads in its link interface but
# does not find_dependency(Threads) itself, so the target has to already exist by
# the time its config is read. It is reached transitively and early:
# slick-logger -> slick-queue -> slick-shm. Without this, a configure that takes
# the FetchContent path for slick-logger (which happens whenever vcpkg's port is
# older than the version required below) fails at generate time with:
#
#   The link interface of target "slick::shm" contains: Threads::Threads
#   but the target was not found.
#
# Threads was previously found only as a side effect of a later dependency's
# config - too late, and not something to rely on.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(FetchContent)

set(SLICK_LOGGER_ENABLE_SOURCE_LOCATION OFF CACHE BOOL "" FORCE)
find_package(slick-logger 1.1.4 CONFIG QUIET)
if (NOT slick-logger_FOUND)
    message(STATUS "Fetching slick-logger...")
    # Disable examples, tests, and benchmarks for slick_logger
    set(BUILD_SLICK_LOGGER_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_LOGGER_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_LOGGER_BENCHMARKS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-logger
        GIT_REPOSITORY https://github.com/SlickQuant/slick-logger.git
        GIT_TAG v1.1.4
    )
    FetchContent_MakeAvailable(slick-logger)
else()
    message(STATUS "Found slick-logger ${slick-logger_VERSION}: ${slick-logger_DIR}")
endif()

find_package(slick-socket 1.0.6 CONFIG QUIET)
if (NOT slick-socket_FOUND)
    message(STATUS "Fetching slick-socket...")
    # Disable slick-socket example, and tests
    set(BUILD_SLICK_SOCKET_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_SOCKET_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SLICK_SOCKET_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-socket
        GIT_REPOSITORY https://github.com/SlickQuant/slick-socket.git
        GIT_TAG v1.0.6
    )
    FetchContent_MakeAvailable(slick-socket)
else()
    message(STATUS "Found slick-socket ${slick-socket_VERSION}: ${slick-socket_DIR}")
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
    message(STATUS "Found slick-object-pool ${slick-object-pool_VERSION}: ${slick-object-pool_DIR}")
endif()

find_package(coinbase-advanced-cpp 1.0.0 CONFIG QUIET)
if (NOT coinbase-advanced-cpp_FOUND)
    message(STATUS "Fetching coinbase-advanced-cpp...")
    FetchContent_Declare(
        coinbase-advanced-cpp
        GIT_REPOSITORY https://github.com/SlickQuant/coinbase-advanced-cpp.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(coinbase-advanced-cpp)
else()
    message(STATUS "Found coinbase-advanced-cpp ${coinbase-advanced-cpp_VERSION}: ${coinbase-advanced-cpp_DIR}")
endif()

find_package(hyperliquid 0.3.0 CONFIG QUIET)
if (NOT hyperliquid_FOUND)
    message(STATUS "Fetching hyperliquid-cpp...")
    FetchContent_Declare(
        hyperliquid-cpp
        GIT_REPOSITORY https://github.com/SlickQuant/hyperliquid-cpp.git
        GIT_TAG v0.3.0
    )
    FetchContent_MakeAvailable(hyperliquid-cpp)
else()
    message(STATUS "Found hyperliquid ${hyperliquid_VERSION}: ${hyperliquid_DIR}")
endif()

# slick-net should be fetched after coinbase-advanced-cpp/hyperliquid-cpp since it depends on it, and we want to avoid fetching the dependency twice if it's already available
find_package(slick-net CONFIG QUIET)
if (slick-net_FOUND)
    message(STATUS "Found slick-net ${slick-net_VERSION}: ${slick-net_VERSION}")
endif()

find_package(slick-orderbook 1.0.5 CONFIG QUIET)
if (NOT slick-orderbook_FOUND)
    message(STATUS "Fetching slick-orderbook...")
    set(BUILD_COINBASE_ADVANCED_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        slick-orderbook
        GIT_REPOSITORY https://github.com/SlickQuant/slick-orderbook.git
        GIT_TAG v1.0.5
    )
    FetchContent_MakeAvailable(slick-orderbook)
else()
    message(STATUS "Found slick-orderbook ${slick-orderbook_VERSION}: ${slick-orderbook_DIR}")
endif()

# Add QuickFIX dependency (prefer vcpkg installation)
find_package(quickfix CONFIG REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(foonathan_memory CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS uuid multiprecision)

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
        $<$<CONFIG:Release>:-flto>

        # The market-data path reads and writes packed structs through a
        # byte queue (reinterpret_cast<MarketDataUpdate*>(md_queue_[i]), then
        # the data[0] flexible-array payload) in ~30 places. That is a strict
        # aliasing violation, and -O3 with -flto is exactly where GCC starts
        # acting on the assumption that those accesses cannot alias.
        # Debug (-O0) and MSVC (conservative by default) do not, which matches
        # where the failures show up. Keep aliasing analysis off here.
        $<$<CONFIG:Release>:-fno-strict-aliasing>

        # Negligible cost, and without it a release backtrace from the CI
        # gdb step is often unusable.
        $<$<CONFIG:Release>:-fno-omit-frame-pointer>
    )
    if(ENABLE_NATIVE_ARCH)
        message(STATUS "Building for the local CPU (-march=native) - binary is NOT portable")
        add_compile_options($<$<CONFIG:Release>:-march=native>)
    endif()
endif()

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
    # GCC/Clang AddressSanitizer.
    # Release flags stay portable here too; see ENABLE_NATIVE_ARCH above.
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")
  endif()
endif()

add_subdirectory(src)

# Tests
if(BUILD_EXCH_SIMULATOR_TESTING)
  find_package(GTest CONFIG REQUIRED)
  enable_testing()
  add_subdirectory(tests)
endif()

# Doxygen API documentation.
#
# Convenience target for local use: `cmake --build build --target docs`.
# CI does not use it — .github/workflows/documentation.yml runs doxygen
# directly so the docs build does not depend on the full vcpkg/FetchContent
# configure. Output lands in doxygen-out/html (see Doxyfile.in).
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        @ONLY
    )
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen (version ${PROJECT_VERSION})"
        VERBATIM
    )
else()
    message(STATUS "Doxygen not found - `docs` target unavailable")
endif()
