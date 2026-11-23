#include <slick/logger.hpp> 
#include "coinbase_rest_order_gateway.hpp"
#include <coinbase/auth.hpp>
#include <utils/fixed_string.hpp>
#include <algorithm>
#include <cmath>
#include "coinbase_common.hpp"

using namespace slick::sim;
using namespace slick::sim::order_gateway;
using Http = slick::net::Http;

namespace {

bool is_multiple_of(double value, double increment) {
    if (increment < 1e-12) return true;
    double ratio = value / increment;
    return std::abs(ratio - std::round(ratio)) < 1e-6;
}

// Per-request data for HTTP POST handling
struct PostRequestData {
    std::string body_buffer;
    std::string user_id;
};

}  // namespace

namespace {

bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
    return lhs.size() == rhs.size()
        && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a))
                   == std::tolower(static_cast<unsigned char>(b));
           });
}

/// Matches Coinbase's `order_status` filter against a simulator OrderStatus.
///
/// Most values map straight onto to_string(status). The exception is OPEN, which on
/// Coinbase means "still working" rather than any single state, so it has to cover
/// every status an order can sit in while it still has quantity outstanding.
bool matches_order_status(std::string_view filter, OrderStatus status) noexcept {
    if (iequals(filter, "OPEN")) {
        switch (status) {
        case OrderStatus::NEW:
        case OrderStatus::PARTIALLY_FILLED:
        case OrderStatus::PENDING_NEW:
        case OrderStatus::PENDING_CANCEL:
        case OrderStatus::PENDING_REPLACE:
        case OrderStatus::REPLACED:
            return true;
        default:
            return false;
        }
    }
    return iequals(filter, to_string(status));
}

}   // namespace

CoinbaseRestOrderGateway::CoinbaseRestOrderGateway(const json& config, slick::queue<Request> &request_queue, slick::queue<OrderResponse> &response_queue)
    : RestWsOrderGateway(Venue::COINBASE, request_queue, response_queue, config.value("port", 3000))
    , response_cursor_(response_queue.initial_reading_index())
    , response_poll_ms_(config.value("response_poll_ms", 1))
{
    if (config.contains("initial_products")) {
        products_ = config["initial_products"].get<std::string>();
    } else {
        auto base_url = config.value("base_url", "https://api.coinbase.com");
        auto rsp = Http::get(base_url + "/api/v3/brokerage/market/products");
        if (rsp.is_ok()) {
            products_ = rsp.result_text;
        } else {
            LOG_WARN("Failed to get products. error: {}", rsp.result_text);
        }
    }
    if (!products_.empty()) {
        auto products = json::parse(products_);
        for (auto &prod : products["products"]) {
            const auto product_id = prod["product_id"].get<std::string>();
            auto as_double = [&prod](const char *key) -> double {
                if (!prod.contains(key)) return 0.0;
                const auto &value = prod[key];
                return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
            };
            product_specs_[product_id] = ProductSpec{
                .price_increment = as_double("price_increment"),
                .base_increment = as_double("base_increment"),
                .base_min_size = as_double("base_min_size"),
            };
            product_by_id_[product_id] = prod.dump();
        }
    }
}

void CoinbaseRestOrderGateway::on_listen_socket(us_listen_socket_t *) {
    // Create the timer once and arm it for requests. A repeating millisecond timer kept an active
    // handle on the uWS loop, preventing app.run() from returning and causing stop() to hang at
    // join. An idle gateway now has no active timer.
    response_timer_ = us_create_timer(reinterpret_cast<us_loop_t *>(loop_), 0, sizeof(CoinbaseRestOrderGateway *));
    *reinterpret_cast<CoinbaseRestOrderGateway **>(us_timer_ext(response_timer_)) = this;
    LOG_INFO("Coinbase REST response poll ready ({} ms)", response_poll_ms_);
}

void CoinbaseRestOrderGateway::armResponseTimer() {
    if (response_timer_ == nullptr || response_timer_armed_) {
        return;
    }
    response_timer_armed_ = true;
    us_timer_set(
        response_timer_,
        [](us_timer_t *timer) {
            auto *self = *reinterpret_cast<CoinbaseRestOrderGateway **>(us_timer_ext(timer));
            self->response_timer_armed_ = false;
            self->drainResponses();
            self->expireWaiters();
            // Keep polling while HTTP waiters or expected reports remain. A disconnected client can
            // still have a report in flight that supplies order routing. Once both are drained,
            // leave the timer inactive so the loop can exit on shutdown.
            if (self->pending_.size() != 0 || !self->timed_waiters_.empty() ||
                !self->expected_reports_.empty()) {
                self->armResponseTimer();
            }
        },
        std::max(1, response_poll_ms_), 0);   // one-shot
}

void CoinbaseRestOrderGateway::stop() {
    if (loop_ != nullptr && response_timer_ != nullptr) {
        us_timer_t *timer = response_timer_;
        response_timer_ = nullptr;   // armResponseTimer must not touch it again
        response_timer_armed_ = false;
        // Timers are owned by the loop and may only be touched on its thread.
        loop_->defer([timer]() { us_timer_close(timer); });
    }
    RestWsOrderGateway::stop();
}

void CoinbaseRestOrderGateway::track(PendingResponses::Id id, std::chrono::milliseconds timeout) {
    timed_waiters_.push_back({id, std::chrono::steady_clock::now() + timeout});
}

void CoinbaseRestOrderGateway::untrack(PendingResponses::Id id) {
    std::erase_if(timed_waiters_, [id](const TimedWaiter &waiter) { return waiter.id == id; });
}

void CoinbaseRestOrderGateway::abandonPending(const std::shared_ptr<PendingHttp> &http) {
    for (const auto id : http->ids) {
        pending_.abandon(id);
        // abandon() skips the completion that would untrack this id. Remove its timeout record here
        // to avoid keeping the poll timer active for the life of the process.
        untrack(id);
    }
    http->ids.clear();
}

void CoinbaseRestOrderGateway::expectReport(uint64_t tag) {
    // The deadline stops polling if the engine drops a request and never reports. Reports normally
    // arrive in microseconds; reading one removes its entry immediately.
    expected_reports_.push_back({tag, std::chrono::steady_clock::now() + std::chrono::seconds(5)});
}

void CoinbaseRestOrderGateway::expireWaiters() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<PendingResponses::Id> due;
    for (const auto &waiter : timed_waiters_) {
        if (waiter.deadline <= now) {
            due.push_back(waiter.id);
        }
    }
    for (const auto id : due) {
        pending_.expire(id);   // its completion untracks it
    }
    // A report that never arrives must not keep the poll running forever.
    std::erase_if(expected_reports_,
                  [now](const ExpectedReport &expected) { return expected.deadline <= now; });
}

void CoinbaseRestOrderGateway::drainResponses() {
    for (;;) {
        auto [data, size] = response_queue_.read(response_cursor_);
        if (data == nullptr) {
            return;
        }
        const auto &response = *data;
        orders_.apply(response);

        // The request this report answers is no longer owed one, whether or not its
        // HTTP client is still connected.
        if (response.request_tag != 0 && !expected_reports_.empty()) {
            std::erase_if(expected_reports_, [&response](const ExpectedReport &expected) {
                return expected.tag == response.request_tag;
            });
        }

        // Learn routing from the report stream. A disconnect abandons the create handler's
        // completion even when the engine accepts the order. Recording routing in that completion
        // left such orders visible in history but uncancellable.
        const std::string_view order_id(response.order_id,
                                        strnlen(response.order_id, sizeof(response.order_id)));
        if (!order_id.empty()) {
            switch (coinbase::routing_effect(response.order_status)) {
            case coinbase::RoutingEffect::Forget:
                order_id_to_symbol_.erase(std::string(order_id));
                break;
            case coinbase::RoutingEffect::Learn: {
                const std::string_view symbol(response.symbol,
                                              strnlen(response.symbol, sizeof(response.symbol)));
                if (!symbol.empty()) {
                    order_id_to_symbol_[std::string(order_id)] = std::string(symbol);
                }
                break;
            }
            case coinbase::RoutingEffect::Ignore:
                break;
            }
        }

        pending_.dispatch(response);
    }
}

PendingResponses::Id CoinbaseRestOrderGateway::awaitReport(uint64_t tag,
                                                           std::chrono::milliseconds timeout,
                                                           const std::shared_ptr<PendingHttp> &http,
                                                           std::function<void(const OrderResponse *)> on_report) {
    auto id_slot = std::make_shared<PendingResponses::Id>(PendingResponses::kNoId);
    const auto id = pending_.add(
        [tag](const OrderResponse &response) { return response.request_tag == tag; },
        [this, id_slot, http, on_report = std::move(on_report)](const OrderResponse *response) {
            untrack(*id_slot);
            if (!http->aborted) {
                on_report(response);
            }
        });
    *id_slot = id;
    http->ids.push_back(id);
    track(id, timeout);
    armResponseTimer();
    return id;
}

std::tuple<bool, std::string, std::string> CoinbaseRestOrderGateway::authenticate(uWS::HttpRequest *req) {
    // Get the Authorization header
    std::string_view auth_header = req->getHeader("authorization");
    
    // For Coinbase, it's typically: "Bearer <JWT_TOKEN>"
    if (auth_header.empty()) {
        auth_header = req->getHeader("Authorization");
        if (auth_header.empty()) {
            return std::make_tuple(false, "", R"({"error":"UNAUTHORIZED","message":"Authorization header required"})");
        }
    }
    
    // Extract the token (remove "Bearer " prefix)
    std::string token;
    if (auth_header.starts_with("Bearer ")) {
        token = std::string(auth_header.substr(7));
    } else {
        return std::make_tuple(false, "", R"({"error":"UNAUTHORIZED","message":"Invalid auth token"})");
    }

    try {
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);

        // Extract 'sub' claim
        std::string sub = decoded.get_subject();
        std::string user_id = sub.substr(sub.rfind('/') + 1);
        return std::make_tuple(true, user_id, "");

    } catch (const std::exception& /* e */) {
        return std::make_tuple(false, "", R"({"error":"UNAUTHORIZED","message":"Invalid token"})");
    }
}

CoinbaseRestOrderGateway::OrderValidationResult
CoinbaseRestOrderGateway::validate_order(
    const std::string& product_id, double price_d, double qty_d, OrderType order_type) const
{
    auto it = product_specs_.find(product_id);
    if (it == product_specs_.end()) {
        return {false, "INVALID_PRODUCT_ID", "Unknown product: " + product_id};
    }

    const auto &spec = it->second;
    const double price_inc = spec.price_increment;
    const double base_inc  = spec.base_increment;
    const double base_min  = spec.base_min_size;

    if (order_type != OrderType::MARKET && price_inc > 1e-12) {
        if (!is_multiple_of(price_d, price_inc)) {
            return {false, "INVALID_LIMIT_PRICE_POST_ONLY",
                    "price " + std::to_string(price_d) +
                    " is not a multiple of tick size " + std::to_string(price_inc)};
        }
    }

    if (base_min > 1e-12 && qty_d < base_min - 1e-9) {
        return {false, "INVALID_ORDER_CONFIG",
                "qty " + std::to_string(qty_d) +
                " is below minimum order size " + std::to_string(base_min)};
    }

    if (base_inc > 1e-12 && !is_multiple_of(qty_d, base_inc)) {
        return {false, "INVALID_ORDER_CONFIG",
                "qty " + std::to_string(qty_d) +
                " is not a multiple of base increment " + std::to_string(base_inc)};
    }

    return {true, {}, {}};
}

// void CoinbaseRestOrderGateway::start() {
//     thread_ = std::move(std::thread([this]() {
//         auto app = uWS::App();
//         setup_routes(app);
//         app.listen(port_, [this](auto *listen_socket) {
//             listen_socket_ = listen_socket;
//             if (listen_socket) {
//                 LOG_INFO("Coinbase REST OrderGateway started on port {}", port_);
//             } else {
//                 LOG_ERROR("Failed to listen to port");
//             }
//         }).run();
//     }));
// }

void CoinbaseRestOrderGateway::setup_routes(uWS::App &app) {
    app.get("/api/v3/brokerage/market/products", [this](auto *res, auto */* req */) {
        res->writeHeader("Content-Type", "application/json")->end(products_);
    }).get("/api/v3/brokerage/products", [this](auto *res, auto */* req */) {
        res->writeHeader("Content-Type", "application/json")->end(products_);
    }).get("/api/v3/brokerage/products/:product_id", [this](auto *res, auto *req) {
        handle_get_product(res, req);
    }).get("/api/v3/brokerage/market/products/:product_id", [this](auto *res, auto *req) {
        handle_get_product(res, req);
    }).get("/api/v3/brokerage/orders/historical/batch", [this](auto *res, auto *req) {
        handle_get_orders(res, req);
    }).post("/api/v3/brokerage/orders", [this](auto *res, auto *req) {
        handle_create_order(res, req);
    }).post("/api/v3/brokerage/orders/batch_cancel", [this](auto *res, auto *req) {
        handle_batch_cancel(res, req);
    }).post("/api/v3/brokerage/orders/edit", [this](auto *res, auto *req) {
        handle_edit_order(res, req);
    }).get("/api/v3/brokerage/transaction_summary", [this](auto *res, auto *req) {
        handle_get_transaction_summary(res, req);
    });
}

void CoinbaseRestOrderGateway::handle_get_product(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    std::string_view product_id = req->getParameter("product_id");
    LOG_INFO("Fetching product: {}", product_id);
    auto iter = product_by_id_.find(std::string(product_id));
    if (iter != product_by_id_.end()) {
        res->end(iter->second);
    }
    else {
        res->writeStatus("400 Bad Request")
           ->writeHeader("Content-Type", "application/json")
           ->end(R"({"error":"INVALID_ARGUMENT","error_details":"valid product_id is required","message":"valid product_id is required"})");
    }
}

void CoinbaseRestOrderGateway::handle_get_orders(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    // Query parameters are read before anything else: `req` is only valid for the
    // duration of this call, and uWebSockets recycles it as soon as we return.
    const std::string product_id(req->getQuery("product_id"));
    const std::string order_side(req->getQuery("order_side"));
    const std::string order_status(req->getQuery("order_status"));
    const std::string limit_param(req->getQuery("limit"));

    size_t limit = std::numeric_limits<size_t>::max();
    if (!limit_param.empty()) {
        char *parse_end = nullptr;
        const auto parsed = std::strtoull(limit_param.c_str(), &parse_end, 10);
        if (parse_end == limit_param.c_str() || *parse_end != '\0') {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"INVALID_ARGUMENT","error_details":"limit must be a number","message":"limit must be a number"})");
            return;
        }
        limit = static_cast<size_t>(parsed);
    }

    // Catch up on everything the exchange has reported since the last listing. Done
    // here rather than on a timer because the create-order handler blocks this same
    // loop thread while it waits for its response, so scheduled work is not reliably
    // able to run between requests.
    drainResponses();

    json orders = json::array();
    for (const Order *order : orders_.orders_of(user_id)) {
        if (!product_id.empty() && product_id != order->symbol.view()) {
            continue;
        }
        if (!order_side.empty() && !iequals(order_side, to_string(order->side))) {
            continue;
        }
        if (!order_status.empty() && !matches_order_status(order_status, order->status)) {
            continue;
        }
        if (orders.size() >= limit) {
            break;
        }
        orders.push_back(coinbase::to_json(*order));
    }

    // `has_next`/`cursor` are part of the documented envelope. The whole (filtered)
    // result is returned in one page, so the cursor is always empty - a client that
    // pages correctly still terminates.
    json response = {
        {"orders", std::move(orders)},
        {"sequence", "0"},
        {"has_next", false},
        {"cursor", ""}
    };

    res->writeHeader("Content-Type", "application/json")
        ->end(coinbase::dump_json(response));
}

void CoinbaseRestOrderGateway::handle_create_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    auto request_data = std::make_shared<PostRequestData>();
    request_data->user_id = std::move(user_id);

    // Track the PendingResponses ids and connection state for each in-flight HTTP request. Check
    // `aborted` before using the response: uWS keeps it alive after the handler returns, but
    // destroys it when the connection drops.
    auto http = std::make_shared<PendingHttp>();
    http->response = res;
    res->onAborted([this, http]() {
        http->aborted = true;
        abandonPending(http);
        LOG_WARN("HTTP connection aborted during POST /api/v3/brokerage/orders");
    });

    // Accumulate request body chunks
    res->onData([this, res, request_data, http](std::string_view chunk, bool is_last) mutable {

        // Accumulate body chunks
        request_data->body_buffer.append(chunk);

        if (!is_last) {
            return;  // Wait for more data
        }

        // Process complete request body
        try {
            json request_body = json::parse(request_data->body_buffer);

            // Extract required fields
            std::string product_id = request_body["product_id"];
            std::string side_str = request_body["side"];
            std::string client_order_id = request_body["client_order_id"];
            std::string user_id = request_data->user_id;

            // Parse order_configuration to determine order type and TIF
            auto& order_config = request_body["order_configuration"];

            OrderType order_type;
            TimeInForce tif;
            price_t price = NULL_PRICE;
            qty_t qty = 0;

            // Pattern match on nested configuration keys
            if (order_config.contains("limit_limit_gtc")) {
                order_type = OrderType::LIMIT;
                tif = TimeInForce::GOOD_TILL_CANCEL;
                auto& cfg = order_config["limit_limit_gtc"];
                qty = to_qty_t(std::stod(cfg["base_size"].get<std::string>()));
                price = to_price_t(std::stod(cfg["limit_price"].get<std::string>()));
                // post_only is ignored: AddOrderMessage has no field to carry it.
            }
            else if (order_config.contains("market_market_ioc")) {
                order_type = OrderType::MARKET;
                tif = TimeInForce::IMMEDIATE_OR_CANCEL;
                auto& cfg = order_config["market_market_ioc"];
                qty = to_qty_t(std::stod(cfg["base_size"].get<std::string>()));
            }
            else if (order_config.contains("sor_limit_ioc")) {
                order_type = OrderType::LIMIT;
                tif = TimeInForce::IMMEDIATE_OR_CANCEL;
                auto& cfg = order_config["sor_limit_ioc"];
                qty = to_qty_t(std::stod(cfg["base_size"].get<std::string>()));
                price = to_price_t(std::stod(cfg["limit_price"].get<std::string>()));
            }
            else if (order_config.contains("market_market_fok")) {
                auto& cfg = order_config["market_market_fok"];
                tif = TimeInForce::FILL_OR_KILL;
                qty = to_qty_t(std::stod(cfg["base_size"].get<std::string>()));

                // Check if this is LIMIT FOK (has limit_price) or MARKET FOK
                if (cfg.contains("limit_price")) {
                    order_type = OrderType::LIMIT;
                    price = to_price_t(std::stod(cfg["limit_price"].get<std::string>()));
                } else {
                    order_type = OrderType::MARKET;
                }
            }
            else if (order_config.contains("limit_limit_fok")) {
                // coinbase-advanced-cpp sends this configuration for LIMIT orders with
                // FILL_OR_KILL. The gateway previously accepted only market_market_fok and rejected
                // this form before it reached the engine.
                order_type = OrderType::LIMIT;
                tif = TimeInForce::FILL_OR_KILL;
                auto& cfg = order_config["limit_limit_fok"];
                qty = to_qty_t(std::stod(cfg["base_size"].get<std::string>()));
                price = to_price_t(std::stod(cfg["limit_price"].get<std::string>()));
            }
            else {
                json response_json = {
                    {"success", false},
                    {"error_response", {
                        {"message", "Unsupported order_configuration type"},
                        {"error_details", "Unsupported order_configuration type"},
                        {"new_order_failure_reason", "Unsupported order_configuration type"}
                    }}
                };

                res->writeStatus("400 Bad Request")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(response_json.dump());

                LOG_ERROR("Order rejected: user={}, client_order_id={}, reason=Unsupported order_configuration type, order_configuration={}",
                            user_id, client_order_id, order_config.dump());
                return;
            }

            Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;

            // Validate price and qty against product specs before queuing
            {
                const double price_d = (price != NULL_PRICE) ? to_price_double(price) : 0.0;
                const double qty_d   = to_qty_double(qty);
                auto vr = validate_order(product_id, price_d, qty_d, order_type);
                if (!vr.valid) {
                    json response_json = {
                        {"success", false},
                        {"error_response", {
                            {"message",                  vr.error_type},
                            {"error_details",            vr.error_details},
                            {"new_order_failure_reason", vr.error_type}
                        }}
                    };
                    res->writeStatus("400 Bad Request")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(response_json.dump());
                    LOG_ERROR("Order rejected (validation): user={}, client_order_id={}, reason={}",
                              user_id, client_order_id, vr.error_details);
                    return;
                }
            }

            // Use a request tag to correlate reports independently of user and client order ids.
            // The order stores the tag and copies it to every response.
            const uint64_t tag = ++next_request_tag_;

            // Reserve space in request queue
            auto request_index = request_queue_.reserve();
            auto* request = request_queue_[request_index];

            // Populate Request struct
            request->time_stamp = utils::get_current_time_ns();
            request->request_tag = tag;
            utils::copy_wire_field(request->symbol, product_id);
            request->msg_type = MessageType::NEW_ORDER_SINGLE;

            // Populate AddOrderMessage
            utils::copy_wire_field(request->add_order.user_id, user_id);
            if (utils::copy_wire_field(request->add_order.client_order_id, client_order_id)) {
                LOG_WARN("client_order_id truncated to fit into fixed size buffer: original='{}', truncated='{}'", client_order_id, request->add_order.client_order_id);
            }
            request->add_order.side = side;
            request->add_order.type = order_type;
            request->add_order.time_in_force = tif;
            request->add_order.price = price;
            request->add_order.qty = qty;
            request->add_order.take_profit_price = NULL_PRICE;
            request->add_order.stop_loss_price = NULL_PRICE;
            request->add_order.expire_time = 0;

            // Record the expectation before publishing the request. The exchange thread can reply
            // as soon as the request is queued. Registering afterwards could leave an uncleared
            // expectation, an armed poll timer and a loop that cannot drain on shutdown.
            expectReport(tag);
            request_queue_.publish(request_index);

            LOG_INFO("Published order request: user={}, client_order_id={}, symbol={}, side={}, type={}, qty={}, tag={}",
                        user_id, client_order_id, product_id, side_str,
                        to_string(order_type), to_qty_double(qty), tag);

            // The loop timer sends the HTTP response when the report arrives. Waiting here used to
            // block the gateway's only event-loop thread for the full timeout, preventing it from
            // serving other clients.
            awaitReport(
                tag, std::chrono::milliseconds(5000), http,
                [this, res, user_id, client_order_id, product_id, side_str,
                 order_config = order_config.get<json>()](const OrderResponse *matched_response) {
                    if (matched_response == nullptr) {
                        res->writeStatus("504 Gateway Timeout")
                            ->writeHeader("Content-Type", "application/json")
                            ->end(R"({"error":"TIMEOUT","message":"Order processing timeout"})");
                        LOG_ERROR("Order submission timeout: user={}, client_order_id={}", user_id, client_order_id);
                        return;
                    }
                    if (matched_response->order_status == OrderStatus::REJECTED) {
                        // Bounded: this field is a fixed char array in a recycled
                        // queue slot, not a guaranteed NUL-terminated string.
                        const std::string error_msg(
                            matched_response->error_message,
                            strnlen(matched_response->error_message, sizeof(matched_response->error_message)));
                        json response_json = {
                            {"success", false},
                            {"error_response", {
                                {"message", to_string(matched_response->reject_reason)},
                                {"error_details", error_msg},
                                {"new_order_failure_reason", to_string(matched_response->reject_reason)}
                            }}
                        };
                        res->writeStatus("400 Bad Request")
                            ->writeHeader("Content-Type", "application/json")
                            ->end(response_json.dump());
                        LOG_ERROR("Order rejected: user={}, client_order_id={}, reason={}",
                                    user_id, client_order_id, to_string(matched_response->reject_reason));
                        return;
                    }

                    json response_json = {
                        {"success", true},
                        {"success_response", {
                            {"order_id", matched_response->order_id},
                            {"product_id", product_id},
                            {"side", side_str},
                            {"client_order_id", client_order_id}
                        }},
                        {"order_configuration", order_config}
                    };
                    res->writeStatus("200 OK")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(response_json.dump());

                    LOG_INFO("Order processed successfully: user={}, client_order_id={}, order_id={}, status={}",
                                user_id, client_order_id, std::string_view(matched_response->order_id, sizeof(matched_response->order_id)), to_string(matched_response->order_status));
                });
            return;


        } catch (const json::parse_error& e) {
            abandonPending(http);
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_REQUEST"},
                    {"message", std::string("Invalid JSON: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON parse error in POST /orders: {}", e.what());
        } catch (const json::exception& e) {
            abandonPending(http);
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_ARGUMENT"},
                    {"message", std::string("Missing or invalid field: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON field error in POST /orders: {}", e.what());
        } catch (const std::exception& e) {
            abandonPending(http);
            res->writeStatus("500 Internal Server Error")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INTERNAL_ERROR"},
                    {"message", std::string("Unexpected error: ") + e.what()}
                }).dump());
            LOG_ERROR("Unexpected error in POST /orders: {}", e.what());
        }
    });
}

void CoinbaseRestOrderGateway::handle_batch_cancel(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    auto request_data = std::make_shared<PostRequestData>();
    request_data->user_id = std::move(user_id);

    auto http = std::make_shared<PendingHttp>();
    http->response = res;
    res->onAborted([this, http]() {
        http->aborted = true;
        abandonPending(http);
        LOG_WARN("HTTP connection aborted during POST /api/v3/brokerage/orders/batch_cancel");
    });

    res->onData([this, res, request_data, http](std::string_view chunk, bool is_last) mutable {
        request_data->body_buffer.append(chunk);
        if (!is_last) return;

        try {
            json request_body = json::parse(request_data->body_buffer);
            auto& order_ids_json = request_body["order_ids"];
            std::string user_id = request_data->user_id;

            if (!order_ids_json.is_array() || order_ids_json.empty()) {
                res->writeStatus("400 Bad Request")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"INVALID_ARGUMENT","message":"order_ids must be a non-empty array"})");
                return;
            }

            // Validate every entry before publishing any cancel. A malformed entry
            // ({"order_ids":["id",123]}) previously threw after earlier entries registered waiters:
            // the catch returned 400, then the batch wrote another response on the same connection.
            for (const auto &entry : order_ids_json) {
                if (!entry.is_string()) {
                    res->writeStatus("400 Bad Request")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"INVALID_ARGUMENT","message":"order_ids must contain only strings"})");
                    LOG_ERROR("batch_cancel: non-string order_id in request from user={}", user_id);
                    return;
                }
            }

            struct CancelEntry {
                std::string order_id;
                bool submitted;
                OrdRejectReason reject_reason = OrdRejectReason::NONE;
            };
            struct Batch {
                uWS::HttpResponse<false>* res;
                std::vector<CancelEntry> entries;
                size_t remaining;
                std::string user_id;
                /// Complete the batch once. A late report after an error response must not append a
                /// second HTTP response to the connection.
                bool finished = false;
            };
            auto batch = std::make_shared<Batch>(Batch{res, {}, 0, user_id, false});
            batch->entries.reserve(order_ids_json.size());

            auto finish = [this, batch]() {
                if (batch->finished) {
                    return;
                }
                batch->finished = true;
                json results = json::array();
                int rejected = 0;
                for (auto& entry : batch->entries) {
                    if (!entry.submitted) {
                        results.push_back({{"success", false}, {"failure_reason", "UNKNOWN_ORDER"}, {"order_id", entry.order_id}});
                        continue;
                    }
                    if (entry.reject_reason != OrdRejectReason::NONE) {
                        ++rejected;
                        results.push_back({{"success", false},
                                           {"failure_reason", to_string(entry.reject_reason)},
                                           {"order_id", entry.order_id}});
                    } else {
                        results.push_back({{"success", true}, {"failure_reason", ""}, {"order_id", entry.order_id}});
                        order_id_to_symbol_.erase(entry.order_id);
                    }
                }
                batch->res->writeStatus("200 OK")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(json({{"results", results}}).dump());
                LOG_INFO("batch_cancel completed: user={}, total={}, rejected={}",
                         batch->user_id, batch->entries.size(), rejected);
            };

            // Drain reports before looking up routing. A report from an abandoned request may have
            // arrived since the last poll; until it is read, a cancel for that live order would
            // return UNKNOWN_ORDER.
            drainResponses();

            // Publish the whole batch before awaiting replies. The old loop waited 200 ms per order
            // and blocked the gateway's only event-loop thread for a second on a five-order batch.
            // Tags correlate each entry with its accepted or rejected response. A timeout retains
            // the previous "no news is success" behavior.
            for (auto& oid_json : order_ids_json) {
                // Already validated as a string above; get_ref avoids another throw
                // in the middle of a half-submitted batch.
                std::string order_id = oid_json.get_ref<const std::string&>();
                auto sym_it = order_id_to_symbol_.find(order_id);
                if (sym_it == order_id_to_symbol_.end()) {
                    LOG_WARN("batch_cancel: unknown order_id={}", order_id);
                    batch->entries.push_back({order_id, false});
                    continue;
                }
                const std::string symbol = sym_it->second;
                const uint64_t tag = ++next_request_tag_;

                auto request_index = request_queue_.reserve();
                auto* request = request_queue_[request_index];
                request->time_stamp = utils::get_current_time_ns();
                request->request_tag = tag;
                utils::copy_wire_field(request->symbol, symbol);
                request->msg_type = MessageType::ORDER_CANCEL_REQUEST;

                std::memset(&request->cancel_order, 0, sizeof(request->cancel_order));
                utils::copy_wire_field(request->cancel_order.user_id, user_id);
                // Leave client_order_id empty - engine falls through to findOrderByOrderId
                utils::copy_wire_field(request->cancel_order.order_id, order_id);

                // Before publishing, for the same reason as the create path.
                expectReport(tag);
                request_queue_.publish(request_index);
                LOG_INFO("Submitted cancel request: user={}, order_id={}, symbol={}, tag={}", user_id, order_id, symbol, tag);

                batch->entries.push_back({order_id, true, OrdRejectReason::NONE});
                ++batch->remaining;
                const size_t index = batch->entries.size() - 1;
                awaitReport(tag, std::chrono::milliseconds(500), http,
                            [batch, index, finish](const OrderResponse *response) {
                                if (response != nullptr && response->order_status == OrderStatus::REJECTED) {
                                    batch->entries[index].reject_reason = response->reject_reason;
                                }
                                if (--batch->remaining == 0) {
                                    finish();
                                }
                            });
            }

            if (batch->remaining == 0) {
                finish();
            }
        } catch (const json::parse_error& e) {
            abandonPending(http);
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_REQUEST"},
                    {"message", std::string("Invalid JSON: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON parse error in POST /orders/batch_cancel: {}", e.what());
        } catch (const json::exception& e) {
            abandonPending(http);
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_ARGUMENT"},
                    {"message", std::string("Missing or invalid field: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON field error in POST /orders/batch_cancel: {}", e.what());
        } catch (const std::exception& e) {
            abandonPending(http);
            res->writeStatus("500 Internal Server Error")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INTERNAL_ERROR"},
                    {"message", std::string("Unexpected error: ") + e.what()}
                }).dump());
            LOG_ERROR("Unexpected error in POST /orders/batch_cancel: {}", e.what());
        }
    });
}

void CoinbaseRestOrderGateway::handle_edit_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    auto request_data = std::make_shared<PostRequestData>();
    request_data->user_id = std::move(user_id);

    res->onAborted([request_data]() {
        LOG_WARN("HTTP connection aborted during POST /api/v3/brokerage/orders/edit");
    });

    res->onData([this, res, request_data](std::string_view chunk, bool is_last) mutable {
        request_data->body_buffer.append(chunk);
        if (!is_last) return;

        try {
            json request_body = json::parse(request_data->body_buffer);
            std::string order_id = request_body["order_id"];
            std::string user_id = request_data->user_id;

            // Drain unread reports before looking up routing, as in the cancel path.
            drainResponses();

            auto sym_it = order_id_to_symbol_.find(order_id);
            if (sym_it == order_id_to_symbol_.end()) {
                res->writeStatus("400 Bad Request")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(json({
                        {"success", false},
                        {"errors", json::array({{{"edit_failure_reason", "UNKNOWN_ORDER"}}})}
                    }).dump());
                LOG_WARN("edit_order: unknown order_id={}", order_id);
                return;
            }
            const std::string& symbol = sym_it->second;

            price_t new_price = NULL_PRICE;
            qty_t new_qty = 0;
            if (request_body.contains("price")) {
                new_price = to_price_t(std::stod(request_body["price"].get<std::string>()));
            }
            if (request_body.contains("size")) {
                new_qty = to_qty_t(std::stod(request_body["size"].get<std::string>()));
            }

            // Validate modified price/qty against product specs before queuing
            if (new_price != NULL_PRICE || new_qty != 0) {
                const OrderType ot   = (new_price != NULL_PRICE) ? OrderType::LIMIT : OrderType::MARKET;
                const double price_d = (new_price != NULL_PRICE) ? to_price_double(new_price) : 0.0;
                const double qty_d   = (new_qty != 0) ? to_qty_double(new_qty) : 0.0;
                auto vr = validate_order(symbol, price_d, qty_d, ot);
                if (!vr.valid) {
                    res->writeStatus("400 Bad Request")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(json({
                            {"success", false},
                            {"errors", json::array({{{"edit_failure_reason", vr.error_type}}})}
                        }).dump());
                    LOG_ERROR("Edit order rejected (validation): user={}, order_id={}, reason={}",
                              user_id, order_id, vr.error_details);
                    return;
                }
            }

            uint64_t response_read_index = response_queue_.initial_reading_index();

            auto request_index = request_queue_.reserve();
            auto* request = request_queue_[request_index];
            request->time_stamp = utils::get_current_time_ns();
            utils::copy_wire_field(request->symbol, symbol);
            request->msg_type = MessageType::ORDER_REPLACE_REQUEST;

            std::memset(&request->modify_order, 0, sizeof(request->modify_order));
            utils::copy_wire_field(request->modify_order.user_id, user_id);
            utils::copy_wire_field(request->modify_order.order_id, order_id);
            request->modify_order.new_price = new_price;
            request->modify_order.new_qty = new_qty;

            request_queue_.publish(request_index);
            LOG_INFO("Published edit request: user={}, order_id={}, symbol={}", user_id, order_id, symbol);

            // Wait briefly for a reject. No response within timeout means success.
            constexpr int timeout_ms = 200;
            constexpr int poll_interval_us = 100;
            OrdRejectReason reject_reason = OrdRejectReason::UNKNOWN;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

            std::string client_order_id;
            while (std::chrono::steady_clock::now() < deadline) {
                auto [data, size] = response_queue_.read(response_read_index);
                if (data != nullptr) {
                    if (std::string(data->user_id) == user_id && data->request_time == request->time_stamp) {
                        if (data->order_status == OrderStatus::REJECTED) {
                            reject_reason = data->reject_reason;
                        }
                        else {
                            reject_reason = OrdRejectReason::NONE;
                        }
                        client_order_id = data->client_order_id;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(poll_interval_us));
            }

            if (reject_reason != OrdRejectReason::NONE) {
                res->writeStatus("400 Bad Request")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(json({
                        {"success", false},
                        {"errors", json::array({{{"edit_failure_reason", to_string(reject_reason)}}})}
                    }).dump());
                LOG_ERROR("Edit order rejected: user={}, order_id={}, reason={}", user_id, order_id, to_string(reject_reason));
            } else {
                res->writeStatus("200 OK")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(json({
                        {"success", true},
                        {"errors", json::array()}
                    }).dump());
                LOG_INFO("Edit order succeeded: user={}, order_id={}, client_order_id={}", user_id, order_id, client_order_id);
            }

        } catch (const json::parse_error& e) {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_REQUEST"},
                    {"message", std::string("Invalid JSON: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON parse error in POST /orders/edit: {}", e.what());
        } catch (const json::exception& e) {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_ARGUMENT"},
                    {"message", std::string("Missing or invalid field: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON field error in POST /orders/edit: {}", e.what());
        } catch (const std::exception& e) {
            res->writeStatus("500 Internal Server Error")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INTERNAL_ERROR"},
                    {"message", std::string("Unexpected error: ") + e.what()}
                }).dump());
            LOG_ERROR("Unexpected error in POST /orders/edit: {}", e.what());
        }
    });
}

void CoinbaseRestOrderGateway::handle_get_transaction_summary(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    auto rsp = Http::get("https://api.coinbase.com/api/v3/brokerage/transaction_summary", {
            {"Authorization", "Bearer " + ::coinbase::generate_coinbase_jwt("GET api.coinbase.com/api/v3/brokerage/transaction_summary")}
        });
    res->writeStatus(std::to_string(rsp.result_code) + " " + rsp.reason)
        ->writeHeader("Content-Type", "application/json")
        ->end(rsp.result_text);
}