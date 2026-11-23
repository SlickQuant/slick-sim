#include <order_gateway/rest_ws_order_gateway.hpp>
#include <order_gateway/order_store.hpp>
#include <order_gateway/pending_responses.hpp>
#include <slick/net/http.hpp>
#include <nlohmann/json.hpp>
// #define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct us_listen_socket_t;
struct us_timer_t;
using json = nlohmann::json;

namespace slick::sim::order_gateway {

class CoinbaseRestOrderGateway : public RestWsOrderGateway {
    // std::thread thread_;
    // us_listen_socket_t* listen_socket_ = nullptr;
    // uint32_t port_ = 3000;
    std::string products_;
    std::unordered_map<std::string, std::string> product_by_id_;

    /// Parse the product values needed by validate_order() once at construction. Previously, each
    /// order required parsing the cached product JSON again.
    struct ProductSpec {
        double price_increment = 0.0;
        double base_increment = 0.0;
        double base_min_size = 0.0;
    };
    std::unordered_map<std::string, ProductSpec> product_specs_;

public:
    CoinbaseRestOrderGateway(const json& config, slick::queue<Request> &request_queue, slick::queue<OrderResponse> &response_queue);
    ~CoinbaseRestOrderGateway() override = default;

    std::string_view type() const noexcept override { return "REST"; }

    /// Closes the response timer before the base closes the listen socket. An active timer prevents
    /// the uWS loop from exiting, so app.run() would not return and the base stop() would hang at
    /// join.
    void stop() override;

    std::tuple<bool, std::string, std::string> authenticate(uWS::HttpRequest *req);

private:
    struct OrderValidationResult {
        bool valid = true;
        std::string error_type;
        std::string error_details;
    };

    std::unordered_map<std::string, std::string> order_id_to_symbol_;

    /// Backs GET /orders/historical/batch. Fed from the execution-report stream on
    /// this gateway's own loop thread - see OrderStore for why the order books
    /// themselves are not an option.
    OrderStore orders_;

    /// State shared with one HTTP request while its report is in flight. uWS owns
    /// the response object; `aborted` and the registered ids make sure a completion
    /// never touches it after the client went away.
    struct PendingHttp {
        uWS::HttpResponse<false> *response = nullptr;
        bool aborted = false;
        std::vector<PendingResponses::Id> ids;
    };
    struct TimedWaiter {
        PendingResponses::Id id;
        std::chrono::steady_clock::time_point deadline;
    };
    /// A request whose report has not been consumed yet.
    struct ExpectedReport {
        uint64_t tag;
        std::chrono::steady_clock::time_point deadline;
    };

    PendingResponses pending_;
    std::vector<TimedWaiter> timed_waiters_;

    /// Track expected engine reports independently of HTTP waiters. Stopping polling after a
    /// disconnect left accepted reports unread, so the gateway had no routing for those orders and
    /// cancels returned UNKNOWN_ORDER until another request drained the stream.
    std::vector<ExpectedReport> expected_reports_;

    /// Where the exchange's report stream has been read to. Shared by the poll timer
    /// and by GET /orders/historical/batch so a response is folded into `orders_`
    /// exactly once.
    uint64_t response_cursor_ = 0;
    us_timer_t *response_timer_ = nullptr;
    /// Whether a one-shot poll is queued. Arm it on demand so an idle gateway has no active timer
    /// handle.
    bool response_timer_armed_ = false;
    uint64_t next_request_tag_ = 0;
    int response_poll_ms_ = 1;

    /// Queues one poll of the report stream if none is pending.
    void armResponseTimer();

    /// Registers `on_report` for the report carrying `tag`, expiring it after
    /// `timeout`. The completion receives nullptr on expiry, and is skipped entirely
    /// when the client has disconnected.
    PendingResponses::Id awaitReport(uint64_t tag,
                                     std::chrono::milliseconds timeout,
                                     const std::shared_ptr<PendingHttp> &http,
                                     std::function<void(const OrderResponse *)> on_report);
    /// Drops this request's waiters and timeout records. `PendingResponses::abandon` skips the
    /// completion so it cannot write to a closed connection. Since that completion normally
    /// untracks the timeout, remove it here to avoid keeping the poll timer active forever.
    void abandonPending(const std::shared_ptr<PendingHttp> &http);
    /// Records that the request tagged `tag` still owes a report. Keeps the stream
    /// drained until it arrives, whether or not the HTTP request survives.
    void expectReport(uint64_t tag);
    void track(PendingResponses::Id id, std::chrono::milliseconds timeout);
    void untrack(PendingResponses::Id id);
    void drainResponses();
    void expireWaiters();

    void on_listen_socket(us_listen_socket_t *listen_socket) override;

    OrderValidationResult validate_order(const std::string& product_id, double price_d, double qty_d, OrderType order_type) const;

    void setup_routes(uWS::App &app) override;
    void handle_get_product(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_get_orders(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_create_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_batch_cancel(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_edit_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_get_transaction_summary(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
};

}