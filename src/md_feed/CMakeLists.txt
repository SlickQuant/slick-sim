# MDFeed is a header-only start()/stop() interface. Implementations live with
# their venues under src/venues/. Keep a separate target for a future historical
# replay feed to implement.
add_library(md_feed INTERFACE)
target_link_libraries(md_feed INTERFACE
    slick_sim_core
)
