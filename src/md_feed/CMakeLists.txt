# MDFeed is a header-only start()/stop() interface. Implementations live with
# their venues under src/venues/ (or in the venue's own repository).
add_library(md_feed INTERFACE)
target_link_libraries(md_feed INTERFACE
    slick_sim_core
)

# pcap replay support: pcap_reader.hpp and pcap_replay_source.hpp are header-only over
# libpcap, so a venue that replays captures links slick_sim_pcap (md_feed carries it too).
# Absent when libpcap is, or when SLICK_SIM_ENABLE_PCAP_FEED is OFF; code that needs it
# checks for the target.
if(SLICK_SIM_ENABLE_PCAP_FEED)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SLICK_SIM_LIBPCAP IMPORTED_TARGET GLOBAL libpcap)
    endif()
    if(SLICK_SIM_LIBPCAP_FOUND)
        message(STATUS "Found libpcap ${SLICK_SIM_LIBPCAP_VERSION}: pcap replay support enabled")
        add_library(slick_sim_pcap INTERFACE)
        add_library(slick::sim_pcap ALIAS slick_sim_pcap)
        target_link_libraries(slick_sim_pcap INTERFACE PkgConfig::SLICK_SIM_LIBPCAP)
        target_link_libraries(md_feed INTERFACE slick_sim_pcap)
    else()
        message(STATUS "libpcap not found: pcap replay support disabled")
    endif()
endif()

# The pcap replay feed base (state machine, pacing, control channel). Venues derive from
# it and add slick_sim_pcap_feed to their DEPENDS. Needs libpcap and the TCP gateway's
# slick-socket for the control channel's TCP server, so it only exists when both are there.
if(TARGET slick_sim_pcap AND SLICK_SIM_ENABLE_TCP_GATEWAY)
    add_library(slick_sim_pcap_feed STATIC pcap_feed.cpp pcap_control_server.cpp)
    add_library(slick::sim_pcap_feed ALIAS slick_sim_pcap_feed)
    # Venue code that offers a pcap feed is compiled under this, so it still builds without one.
    target_compile_definitions(slick_sim_pcap_feed PUBLIC SLICK_SIM_HAS_PCAP_FEED=1)
    target_link_libraries(slick_sim_pcap_feed PUBLIC
        md_feed
        slick_sim_pcap
        slick::socket
    )
endif()
