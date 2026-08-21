# Header-only, for the same reason md_feed is: MarketDataPublisher is an interface
# and SequencedMessage is a header. The one implementation with a body,
# WebsocketMarketDataPublisher, is uWebSockets-based and derived from only by venue
# adapters that serve a WebSocket, so it is compiled into the on-demand
# slick_sim_rest_ws target instead - see slick_sim_require_rest_ws() in
# cmake/SlickSimVenue.cmake. Its header stays here, beside the base it extends.
add_library(market_data_publisher INTERFACE)
target_link_libraries(market_data_publisher INTERFACE
    slick_sim_core
)
