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
    message(STATUS "Adding slick-sim external venue: ${dir}")

    # The venues registered so far, so what this directory adds can be reported - and
    # a directory that adds none (a path to the wrong repository, say) is flagged
    # instead of silently contributing nothing.
    get_property(_slick_sim_before GLOBAL PROPERTY SLICK_SIM_ENABLED_VENUES)
    list(LENGTH _slick_sim_before _slick_sim_before_count)

    # Indent the venue's own output (its dependency lookups) under the line above.
    list(APPEND CMAKE_MESSAGE_INDENT "  ")
    add_subdirectory("${dir}" "${CMAKE_BINARY_DIR}/external_venues/${_slick_sim_external_name}")
    list(POP_BACK CMAKE_MESSAGE_INDENT)

    get_property(_slick_sim_after GLOBAL PROPERTY SLICK_SIM_ENABLED_VENUES)
    list(SUBLIST _slick_sim_after ${_slick_sim_before_count} -1 _slick_sim_added)
    if(_slick_sim_added)
        message(STATUS "slick-sim external venue ${_slick_sim_external_name} registers: ${_slick_sim_added}")
    else()
        message(WARNING "SLICK_SIM_EXTERNAL_VENUES: '${dir}' registered no venue - "
            "its CMakeLists.txt must call slick_sim_add_venue()")
    endif()
endforeach()

get_property(_slick_sim_venues GLOBAL PROPERTY SLICK_SIM_ENABLED_VENUES)
if(_slick_sim_venues)
    message(STATUS "slick-sim venues: ${_slick_sim_venues}")
else()
    message(STATUS "slick-sim venues: none (core only - slick-sim will start no exchanges)")
endif()
