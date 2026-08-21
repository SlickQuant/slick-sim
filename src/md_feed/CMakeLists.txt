# Header-only since the venue split: MDFeed is start()/stop() and every
# implementation now lives with its venue under src/venues/. Kept as a named
# target rather than folded into slick_sim_core because it is the seam a
# historical replay feed will implement.
add_library(md_feed INTERFACE)
target_link_libraries(md_feed INTERFACE
    slick_sim_core
)
