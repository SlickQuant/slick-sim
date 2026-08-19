#include <slick/logger.hpp>
#include "ws_md_publisher.hpp"

using namespace slick::sim;
using namespace slick::sim::md_publisher;

WebsocketMarketDataPublisher::WebsocketMarketDataPublisher(
    Venue venue,
    slick::queue<Request>& request_queue,
    slick::queue<uint8_t>& market_data_queue,
    uint32_t port
) 
    : MarketDataPublisher(venue, request_queue, market_data_queue)
    , port_(port)
{
}

void WebsocketMarketDataPublisher::start() {
    running_.store(true, std::memory_order_release);
    // Start WebSocket server thread (handles everything in event loop)
    ws_thread_ = std::thread([this]() {
        auto app = uWS::App();
        setup_routes(app);
        app.listen(port_, [this](auto *listen_socket) {
            listen_socket_ = listen_socket;
            if (listen_socket) {
                LOG_INFO("{} Publisher started on port {}", to_string(venue_), port_);

                // Get the event loop
                loop_ = uWS::Loop::get();

                // Create heartbeat timer in the event loop
                // Use defer() to create timer with proper lambda capture
                loop_->defer([this]() {
                    // The third argument is ext_size, not a flag: it is the number
                    // of bytes us_timer_ext() hands back. Passing 0 and then
                    // writing a pointer there overruns the timer allocation.
                    heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0,
                                                       sizeof(WebsocketMarketDataPublisher*));

                    // Store 'this' pointer in timer extension data
                    *((WebsocketMarketDataPublisher**)us_timer_ext(heartbeat_timer_)) = this;

                    // Set timer callback that retrieves 'this' from extension data
                    us_timer_set(heartbeat_timer_, [](struct us_timer_t *timer) {
                        // Dereference the pointer stored in extension data
                        auto* self = *((WebsocketMarketDataPublisher**)us_timer_ext(timer));
                        self->check_heartbeats();
                    }, ping_interval_ms_, ping_interval_ms_);
                });

                LOG_INFO("{} Publisher Event loop configured (heartbeat: {}ms)", to_string(venue_), ping_interval_ms_);

                // Start continuous response processing
                schedule_publish_processing();

            } else {
                LOG_ERROR("{} Publisher Failed to listen on port {}", to_string(venue_), port_);
            }
        })
        .run();
    });
}

void WebsocketMarketDataPublisher::stop() {
    running_.store(false, std::memory_order_release);

    // Close heartbeat timer
    if (heartbeat_timer_) {
        us_timer_close(heartbeat_timer_);
        heartbeat_timer_ = nullptr;
    }

    if (listen_socket_ && loop_) {
        // Closing the listen socket must happen on the loop's own thread;
        // doing it cross-thread races with the loop's poll handles.
        loop_->defer([this]() {
            if (listen_socket_) {
                us_listen_socket_close(0, listen_socket_);
                listen_socket_ = nullptr;
            }
        });
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void WebsocketMarketDataPublisher::publish_processing() {
    // Drain up to this many updates before handing the loop back, so socket
    // writes and the heartbeat timer still get serviced under load.
    //
    // Every batch costs one defer, so the limit sets the ratio of scheduling
    // overhead to work: at 100 a flood re-entered the loop once per 100 updates,
    // at 1000 once per 1000. The yield still happens often enough that a client
    // socket is never starved - a batch is bounded by the queue, and the two
    // snapshot types below cut it short on their own because they are far more
    // expensive to encode than an incremental update.
    constexpr int max_batch = 1000;
    int processed = 0;

    while (processed < max_batch) {
        // Try to read from the queue (non-blocking)
        auto [data, size] = md_queue_.read(data_cursor_);

        if (data == nullptr) {
            // No more data available
            break;
        }

        auto *update = reinterpret_cast<MarketDataUpdate*>(data);
        publish_market_data_update(update);

        if (update->type == MDUpdateType::SUB_RESPONSE ||
            update->type == MDUpdateType::BOOK_SNAPSHOT) {
            break;  // A whole book went out - yield rather than start another
        }

        ++processed;
    }

    // Unconditionally, even when the queue came up empty. This defer is the only
    // thing that runs publish_processing() again: md_queue_ is a lock-free queue
    // with no wakeup, and the exchange thread that fills it never touches the
    // loop. Skipping the reschedule on an empty queue - which looks like an easy
    // win - stops the publisher permanently the first time it catches up.
    // schedule_publish_processing() checks the running_ flag.
    schedule_publish_processing();
}

void WebsocketMarketDataPublisher::schedule_publish_processing() {
    if (!running_.load(std::memory_order_acquire) || !loop_) {
        return;
    }

    loop_->defer([this]() {
        publish_processing();
    });
}