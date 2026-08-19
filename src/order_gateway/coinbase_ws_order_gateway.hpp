#include "rest_ws_order_gateway.hpp"
#include "order_store.hpp"
#include <nlohmann/json.hpp>
// #define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <unordered_map>
#include <string>
#include <atomic>
#include <format>
#include <chrono>
#include <iomanip>
#include <sstream>

struct us_listen_socket_t;
using json = nlohmann::json;

namespace slick::sim::order_gateway {

// Per-socket data structure for uWebSockets
struct PerSocketData {
    std::string sub;  // JWT sub claim
    std::string user_id;
    uint64_t seq_num;
    std::chrono::steady_clock::time_point last_pong;  // Track last pong received
    bool authenticated = false;
    bool subscribed_heartbeat = false;
    static uint_fast64_t heartbeat_count;
};

class CoinbaseWebsocketOrderGateway : public RestWsOrderGateway {
    // std::thread ws_thread_;
    // us_listen_socket_t* listen_socket_ = nullptr;
    // uWS::Loop* loop_ = nullptr;
    us_timer_t* heartbeat_timer_ = nullptr;
    // uint32_t port_ = 3000;
    std::atomic_bool running_{false};

    // Response queue read index
    uint64_t response_read_index_ = 0;

    using wsT = uWS::WebSocket<false, true, PerSocketData>;

    // Client connection mapping: user_id -> WebSocket connection
    // No mutex needed - all access is from the single event loop thread
    std::unordered_map<std::string, wsT*> clients_;

    // Heartbeat configuration
    int ping_interval_ms_{15000};  // Send ping every 30 seconds (in milliseconds)
    std::chrono::seconds pong_timeout_{60};   // Disconnect if no pong for 60 seconds

    std::string products_;
    std::unordered_map<std::string, std::string> product_by_id_;
    /// Backs the `user` channel's subscribe snapshot. Fed from process_all_responses(),
    /// which already reads every execution report, so this store keeps no cursor of
    /// its own - see OrderStore.
    ///
    /// Replaces three containers that were declared but never populated: the snapshot
    /// read one of them and so was always empty, whatever the client had traded.
    OrderStore orders_;
    double fee_rate_ = 0.0012; // 1.2%

public:
    CoinbaseWebsocketOrderGateway(const json& config, slick::queue<Request> &request_queue, slick::queue<OrderResponse> &response_queue);
    
    ~CoinbaseWebsocketOrderGateway() override = default;

    std::string_view type() const noexcept override { return "WS"; }

    void start() override {
        running_.store(true, std::memory_order_release);
        RestWsOrderGateway::start();
    }

    // void start() override {
    //     running_.store(true, std::memory_order_release);

    //     // Start WebSocket server thread (handles everything in event loop)
    //     ws_thread_ = std::thread([this]() {
    //         auto app = uWS::App();

    //         app.ws<PerSocketData>("/*", {
    //                 /* WebSocket settings */
    //                 .compression = uWS::SHARED_COMPRESSOR,
    //                 .maxPayloadLength = 16 * 1024,
    //                 .idleTimeout = 120,
    //                 .maxBackpressure = 1 * 1024 * 1024,

    //                 /* WebSocket handlers */
    //                 .upgrade = nullptr,

    //                 .open = [this](auto *ws) {
    //                     // Initialize connection timestamp
    //                     ws->getUserData()->last_pong = std::chrono::steady_clock::now();
    //                     ws->getUserData()->authenticated = false;
    //                     LOG_INFO("WebSocket client connected");
    //                 },

    //                 .message = [this](auto *ws, std::string_view message, uWS::OpCode /* opCode */) {
    //                     handleMessage(ws, message);
    //                 },

    //                 .drain = [](auto */* ws */) {
    //                     // Check backpressure
    //                 },

    //                 .ping = [](auto *ws, std::string_view /* message */) {
    //                     // Automatically respond to client ping with pong
    //                     // uWebSockets handles this automatically, but we log it
    //                     LOG_TRACE("Received ping from client {:p}", (void*)ws);
    //                 },

    //                 .pong = [this](auto *ws, std::string_view /* message */) {
    //                     // Client responded to our ping
    //                     ws->getUserData()->last_pong = std::chrono::steady_clock::now();
    //                     LOG_TRACE("Received pong from client {:p}", (void*)ws);
    //                 },

    //                 .close = [this](auto *ws, int code, std::string_view message) {
    //                     handleClose(ws, code, message);
    //                 }
    //             })
    //             .listen(port_, [this, &app](auto *listen_socket) {
    //                 listen_socket_ = listen_socket;
    //                 if (listen_socket) {
    //                     LOG_INFO("Coinbase WebSocket OrderGateway started on port {}", port_);

    //                     // Get the event loop
    //                     loop_ = uWS::Loop::get();

    //                     // Create heartbeat timer in the event loop
    //                     // Use defer() to create timer with proper lambda capture
    //                     loop_->defer([this]() {
    //                         heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0, 0);

    //                         // Store 'this' pointer in timer extension data
    //                         *((CoinbaseWebsocketOrderGateway**)us_timer_ext(heartbeat_timer_)) = this;

    //                         // Set timer callback that retrieves 'this' from extension data
    //                         us_timer_set(heartbeat_timer_, [](struct us_timer_t *timer) {
    //                             // Dereference the pointer stored in extension data
    //                             auto* self = *((CoinbaseWebsocketOrderGateway**)us_timer_ext(timer));
    //                             self->checkHeartbeats();
    //                         }, ping_interval_ms_, ping_interval_ms_);
    //                     });

    //                     LOG_INFO("Event loop configured (heartbeat: {}ms)", ping_interval_ms_);

    //                     // Start continuous response processing
    //                     scheduleResponseProcessing();

    //                 } else {
    //                     LOG_ERROR("Failed to listen on port {}", port_);
    //                 }
    //             })
    //             .run();
    //     });
    // }

    void stop() override {
        running_.store(false, std::memory_order_release);

        // Close heartbeat timer
        if (heartbeat_timer_) {
            us_timer_close(heartbeat_timer_);
            heartbeat_timer_ = nullptr;
        }

        RestWsOrderGateway::stop();
        
        // Clear client connections (no lock needed - thread already joined)
        clients_.clear();
    }

private:
    void setup_routes(uWS::App &app) override;

    void on_listen_socket(us_listen_socket_t *listen_socket) override;

    // Handle incoming WebSocket messages
    void handle_message(wsT *ws, std::string_view message);

    // Handle WebSocket close event
    void handle_close(wsT *ws, int code, std::string_view message);

    // Send error message to client
    void send_error(wsT *ws, const std::string& error_msg);

    // Schedule response processing to run in the event loop
    void schedule_response_processing() {
        if (!running_.load(std::memory_order_acquire) || !loop_) {
            return;
        }

        loop_->defer([this]() {
            process_all_responses();
        });
    }

    // Process all available responses from the queue
    // Called continuously via defer() or after WebSocket events
    void process_all_responses();

    void handle_order_response(const OrderResponse &response, wsT *ws);

    // Heartbeat check - called by timer in event loop
    void check_heartbeats();
};

}