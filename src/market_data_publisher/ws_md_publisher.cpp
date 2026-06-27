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
                    heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0, 0);

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
    // Process up to 100 update per call to avoid blocking event loop too long
    constexpr int max_batch = 100;
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
            processed = max_batch;
        } else {
            ++processed;
        }
    }

    // Reschedule ourselves to run again on next event loop iteration
    // schedulePublishProcessing() checks running_ flag
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