# The CLI port. CliEndpoint and the reject commands are header-only over slick::queue,
# so the exchange and the venue gateways embed them without this target. The server
# needs slick-socket, which only the TCP gateway build brings in; main.cpp checks
# SLICK_SIM_HAS_CLI.
if(SLICK_SIM_ENABLE_TCP_GATEWAY)
    add_library(slick_sim_cli STATIC cli_server.cpp)
    add_library(slick::sim_cli ALIAS slick_sim_cli)
    target_compile_definitions(slick_sim_cli PUBLIC SLICK_SIM_HAS_CLI=1)
    target_link_libraries(slick_sim_cli PUBLIC
        slick_sim_core
        slick::socket
    )
endif()
