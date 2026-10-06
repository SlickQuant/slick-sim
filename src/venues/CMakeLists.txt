if(SLICK_SIM_ENABLE_COINBASE)
    add_subdirectory(coinbase)
endif()
if(SLICK_SIM_ENABLE_HYPERLIQUID)
    add_subdirectory(hyperliquid)
endif()

# Out-of-tree venues: directories that call slick_sim_add_venue() exactly like the
# in-tree ones, kept in their own repositories. Each is built into the binary and
# its TESTS into slick_sim_tests. Set it from the command line or a git-ignored
# CMakeUserPresets.json, so no local path is ever committed:
#
#   cmake -S . -B build -DSLICK_SIM_EXTERNAL_VENUES="/path/to/venue-a;/path/to/venue-b"
set(SLICK_SIM_EXTERNAL_VENUES "" CACHE STRING "Out-of-tree venue directories, ;-separated")
foreach(dir IN LISTS SLICK_SIM_EXTERNAL_VENUES)
    if(NOT EXISTS "${dir}/CMakeLists.txt")
        message(FATAL_ERROR "SLICK_SIM_EXTERNAL_VENUES: no CMakeLists.txt in '${dir}'")
    endif()
    get_filename_component(_slick_sim_external_name "${dir}" NAME)
    add_subdirectory("${dir}" "${CMAKE_BINARY_DIR}/external_venues/${_slick_sim_external_name}")
endforeach()

get_property(_slick_sim_venues GLOBAL PROPERTY SLICK_SIM_ENABLED_VENUES)
if(_slick_sim_venues)
    message(STATUS "slick-sim venues: ${_slick_sim_venues}")
else()
    message(STATUS "slick-sim venues: none (core only - slick-sim will start no exchanges)")
endif()
