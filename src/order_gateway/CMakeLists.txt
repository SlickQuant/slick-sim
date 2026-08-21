# What is left with sources here is the TCP/FIX/SBE subsystem. The OrderGateway base,
# OrderStore and PendingResponses are header-only, and RestWsOrderGateway - the one
# uWebSockets translation unit - is compiled into the on-demand slick_sim_rest_ws
# target, since only a venue adapter that serves HTTP/WebSocket derives from it. Its
# header stays here beside the base it extends.
#
# The TCP gateway and its three wire parsers are a self-contained subsystem that
# nothing constructs at runtime - no venue uses them, and main.cpp's include is
# commented out. They are the only reason QuickFIX is a dependency at all, so
# turning them off removes a heavyweight find_package from a build that never
# executes a line of the code it provides. Default ON, so nothing changes unless
# it is asked for. With them off the target has no sources left at all, hence the
# INTERFACE branch - `exchange` and the test binary link it either way.
if(SLICK_SIM_ENABLE_TCP_GATEWAY)
    add_library(order_gateway STATIC
        tcp_order_gateway.cpp
        message_parser.cpp
        fix_parser.cpp
        sbe_parser.cpp
        json_parser.cpp
    )

    # sbe/ is needed only by sbe_parser.cpp. These used to be PUBLIC alongside `.`
    # and `..`, and those two were the only reason any other target could resolve
    # <common/...> - that accident is slick_sim_core's job now.
    target_include_directories(order_gateway PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/sbe
    )

    # slick::socket is the TCP server this subsystem is built on, and nothing else in
    # the project uses it - so it follows the same option.
    target_link_libraries(order_gateway PUBLIC
        slick_sim_core
        slick::socket
        quickfix
    )
else()
    add_library(order_gateway INTERFACE)
    target_link_libraries(order_gateway INTERFACE
        slick_sim_core
    )
endif()
