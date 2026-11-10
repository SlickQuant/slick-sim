# The OrderGateway base, OrderStore and PendingResponses are header-only, and
# RestWsOrderGateway - the one uWebSockets translation unit - is compiled into the
# on-demand slick_sim_rest_ws target, since only a venue adapter that serves
# HTTP/WebSocket derives from it. Its header stays here beside the base it extends.
#
# What can have sources here are two independent, optional pieces:
#
#  - TcpOrderGateway (SLICK_SIM_ENABLE_TCP_GATEWAY): the transport base for a venue
#    on a binary session protocol - its order gateway derives from it. It needs
#    slick-socket and nothing else.
#  - The FIX and JSON message parsers (SLICK_SIM_ENABLE_FIX_JSON_PARSERS): an older,
#    unused generic parsing layer. They are the only reason QuickFIX is ever a
#    dependency, so they sit behind their own option and their own target rather
#    than making every TCP venue pull QuickFIX in.
#
# With the TCP gateway off the target has no sources at all, hence the INTERFACE
# branch - `exchange` and the test binary link it either way.
if(SLICK_SIM_ENABLE_TCP_GATEWAY)
    add_library(order_gateway STATIC
        tcp_order_gateway.cpp
    )
    target_include_directories(order_gateway PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # PUBLIC: tcp_order_gateway.hpp includes the slick-socket server it derives from.
    target_link_libraries(order_gateway PUBLIC
        slick_sim_core
        slick::socket
    )
else()
    add_library(order_gateway INTERFACE)
    target_link_libraries(order_gateway INTERFACE
        slick_sim_core
    )
endif()

if(SLICK_SIM_ENABLE_FIX_JSON_PARSERS)
    add_library(slick_sim_message_parsers STATIC
        message_parser.cpp
        fix_parser.cpp
        json_parser.cpp
    )
    target_include_directories(slick_sim_message_parsers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(slick_sim_message_parsers PUBLIC
        slick_sim_core
        quickfix
    )
endif()
