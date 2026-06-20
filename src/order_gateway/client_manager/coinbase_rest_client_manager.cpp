#include "coinbase_rest_client_manager.hpp"
#include <coinbase/auth.hpp>
#include <cmath>

using namespace slick::sim::order_gateway::coinbase;

namespace {

bool is_multiple_of(double value, double increment) {
    if (increment < 1e-12) return true;
    double ratio = value / increment;
    return std::abs(ratio - std::round(ratio)) < 1e-6;
}

}   // namespace

std::tuple<bool, std::string, std::string> CoinbaseRestClientManager::authenticate(uWS::HttpRequest *req) {
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

CoinbaseRestClientManager::OrderValidationResult
CoinbaseRestClientManager::validate_order(
    const std::string& product_id, double price_d, double qty_d, OrderType order_type) const
{
    auto it = product_by_id_.find(product_id);
    if (it == product_by_id_.end()) {
        return {false, "INVALID_PRODUCT_ID", "Unknown product: " + product_id};
    }

    auto pj = json::parse(it->second);
    auto get_d = [&](const char* key) -> double {
        if (!pj.contains(key)) return 0.0;
        auto& v = pj[key];
        return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
    };

    const double price_inc = get_d("price_increment");
    const double base_inc  = get_d("base_increment");
    const double base_min  = get_d("base_min_size");

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

void CoinbaseRestClientManager::start() {
    thread_ = std::move(std::thread([this]() {
        auto app = uWS::App();
        setup_routes(app);
        app.listen(port_, [this](auto *listen_socket) {
            listen_socket_ = listen_socket;
            if (listen_socket) {
                LOG_INFO("Coinbase REST OrderGateway started on port {}", port_);
            } else {
                LOG_ERROR("Failed to listen to port");
            }
        }).run();
    }));
}

void CoinbaseRestClientManager::setup_routes(uWS::App &app) {
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

void CoinbaseRestClientManager::handle_get_product(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
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

void CoinbaseRestClientManager::handle_get_orders(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    json response = {
        {"orders", json::array()}
    };

    // TODO: get orders from order gateway's internal state or database instead of maintaining separate state in REST client manager
}

void CoinbaseRestClientManager::handle_create_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    auto [authenticated, user_id, error_response] = authenticate(req);
    if (!authenticated) {
        res->writeStatus("401 Unauthorized")
            ->writeHeader("Content-Type", "application/json")
            ->end(error_response);
        return;
    }

    auto request_data = std::make_shared<PostRequestData>();
    request_data->user_id = std::move(user_id);

    // Handle connection abort - shared_ptr will handle cleanup
    res->onAborted([request_data]() {
        LOG_WARN("HTTP connection aborted during POST /api/v3/brokerage/orders");
    });

    // Accumulate request body chunks
    res->onData([this, res, request_data](std::string_view chunk, bool is_last) mutable {

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
            bool post_only = false;

            // Pattern match on nested configuration keys
            if (order_config.contains("limit_limit_gtc")) {
                order_type = OrderType::LIMIT;
                tif = TimeInForce::GOOD_TILL_CANCEL;
                auto& cfg = order_config["limit_limit_gtc"];
                qty = to_qty_t(std::stod(cfg["base_size"].get<std::string>()));
                price = to_price_t(std::stod(cfg["limit_price"].get<std::string>()));
                post_only = cfg.value("post_only", false);
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

            // Reserve space in request queue
            auto request_index = request_queue_.reserve();
            auto* request = request_queue_[request_index];

            // Populate Request struct
            request->time_stamp = utils::get_current_time_ns();
            std::memset(request->symbol, 0, sizeof(request->symbol));
            std::memcpy(request->symbol, product_id.c_str(),
                        std::min(sizeof(request->symbol), product_id.size()));
            request->msg_type = MessageType::NEW_ORDER_SINGLE;

            // Populate AddOrderMessage
            std::memset(request->add_order.user_id, 0, sizeof(request->add_order.user_id));
            std::memcpy(request->add_order.user_id, user_id.c_str(),
                        std::min(sizeof(request->add_order.user_id), user_id.size()));
            std::memset(request->add_order.client_order_id, 0, sizeof(request->add_order.client_order_id));
            std::memcpy(request->add_order.client_order_id, client_order_id.c_str(),
                        std::min(sizeof(request->add_order.client_order_id), client_order_id.size()));
            if (client_order_id.size() >= sizeof(request->add_order.client_order_id)) {
                request->add_order.client_order_id[sizeof(request->add_order.client_order_id) - 1] = '\0';
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

            // Publish to queue
            request_queue_.publish(request_index);
            uint64_t response_read_index = response_queue_.initial_reading_index();

            LOG_INFO("Published order request: user={}, client_order_id={}, symbol={}, side={}, type={}, qty={}, response_read_index={}", request_data->
                        user_id, client_order_id, product_id, side_str,
                        to_string(order_type), to_qty_double(qty), response_read_index);

            // Wait for response from matching engine
            constexpr int timeout_ms = 5000;  // 5 second timeout
            constexpr int poll_interval_us = 100;  // Poll every 100 microseconds
            int elapsed_us = 0;

            OrderResponse* matched_response = nullptr;

            while (elapsed_us < timeout_ms * 1000) {
                auto [data, size] = response_queue_.read(response_read_index);

                if (data != nullptr) {
                    // Check if this response matches our request
                    if (std::string(data->user_id) == user_id &&
                        std::string(data->client_order_id) == client_order_id) {
                        matched_response = data;
                        assert(matched_response->response_type == MessageType::EXECUTION_REPORT ||
                               matched_response->response_type == MessageType::REJECT);
                        assert(matched_response->order_status == OrderStatus::PENDING_NEW ||
                               matched_response->order_status == OrderStatus::REJECTED);
                        break;
                    }
                }

                // Sleep briefly to avoid CPU spinning
                std::this_thread::sleep_for(std::chrono::microseconds(poll_interval_us));
                elapsed_us += poll_interval_us;
            }

            if (!matched_response) {
                // Timeout - matching engine didn't respond
                res->writeStatus("504 Gateway Timeout")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(R"({"error":"TIMEOUT","message":"Order processing timeout"})");
                LOG_ERROR("Order submission timeout: user={}, client_order_id={}", user_id, client_order_id);
                return;
            }

            // Build CreateOrderResponse according to Coinbase API spec
            json response_json;

            if (matched_response->order_status == OrderStatus::REJECTED) {
                // Error response
                std::string error_msg = matched_response->error_message;
                if (error_msg.empty()) {
                    error_msg = to_string(matched_response->reject_reason);
                }

                response_json = {
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
            } else {
                // Success response
                response_json = {
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

                std::string order_id_str(matched_response->order_id,
                    strnlen(matched_response->order_id, sizeof(matched_response->order_id)));
                if (!order_id_str.empty()) {
                    order_id_to_symbol_[order_id_str] = product_id;
                }

                LOG_INFO("Order processed successfully: user={}, client_order_id={}, order_id={}, status={}",
                            user_id, client_order_id, std::string_view(matched_response->order_id, sizeof(matched_response->order_id)), to_string(matched_response->order_status));
            }

        } catch (const json::parse_error& e) {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_REQUEST"},
                    {"message", std::string("Invalid JSON: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON parse error in POST /orders: {}", e.what());
        } catch (const json::exception& e) {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_ARGUMENT"},
                    {"message", std::string("Missing or invalid field: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON field error in POST /orders: {}", e.what());
        } catch (const std::exception& e) {
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

void CoinbaseRestClientManager::handle_batch_cancel(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
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
        LOG_WARN("HTTP connection aborted during POST /api/v3/brokerage/orders/batch_cancel");
    });

    res->onData([this, res, request_data](std::string_view chunk, bool is_last) mutable {
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

            struct CancelEntry {
                std::string order_id;
                bool submitted;
                OrdRejectReason reject_reason = OrdRejectReason::NONE;
            };
            std::vector<CancelEntry> entries;
            entries.reserve(order_ids_json.size());

            // Process cancels one at a time so we can correlate reject responses.
            // The engine looks up by client_order_id first if non-empty, so we must
            // leave client_order_id empty and look up by order_id only.
            // Successful cancels don't generate responses (sendOrderCancelPending is
            // a no-op), so no response within the timeout window means success.
            constexpr int timeout_ms = 200;
            constexpr int poll_interval_us = 100;  // used inside per-order wait loop

            for (auto& oid_json : order_ids_json) {
                std::string order_id = oid_json.get<std::string>();

                auto sym_it = order_id_to_symbol_.find(order_id);
                if (sym_it == order_id_to_symbol_.end()) {
                    LOG_WARN("batch_cancel: unknown order_id={}", order_id);
                    entries.push_back({order_id, false});
                    continue;
                }
                const std::string& symbol = sym_it->second;

                // Capture response index before publishing so we don't miss a fast reject
                uint64_t response_read_index = response_queue_.initial_reading_index();

                auto request_index = request_queue_.reserve();
                auto* request = request_queue_[request_index];
                request->time_stamp = utils::get_current_time_ns();
                std::memset(request->symbol, 0, sizeof(request->symbol));
                std::memcpy(request->symbol, symbol.c_str(), std::min(sizeof(request->symbol), symbol.size()));
                request->msg_type = MessageType::ORDER_CANCEL_REQUEST;

                std::memset(&request->cancel_order, 0, sizeof(request->cancel_order));
                std::memcpy(request->cancel_order.user_id, user_id.c_str(),
                            std::min(sizeof(request->cancel_order.user_id), user_id.size()));
                // Leave client_order_id empty — engine falls through to findOrderByOrderId
                std::memcpy(request->cancel_order.order_id, order_id.c_str(),
                            std::min(sizeof(request->cancel_order.order_id), order_id.size()));

                request_queue_.publish(request_index);
                LOG_INFO("Submitted cancel request: user={}, order_id={}, symbol={}", user_id, order_id, symbol);

                // Wait briefly for a reject. If none arrives, the cancel succeeded.
                OrdRejectReason reject_reason = OrdRejectReason::NONE;
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
                while (std::chrono::steady_clock::now() < deadline) {
                    auto [data, size] = response_queue_.read(response_read_index);
                    if (data != nullptr) {
                        if (std::string(data->user_id) == user_id && data->request_time == request->time_stamp) {
                            if (data->order_status == OrderStatus::REJECTED) {
                                reject_reason = data->reject_reason;
                            }
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(poll_interval_us));
                }

                entries.push_back({order_id, true, reject_reason});
            }

            // Build results
            json results = json::array();
            for (auto& entry : entries) {
                if (!entry.submitted) {
                    results.push_back({
                        {"success", false},
                        {"failure_reason", "UNKNOWN_ORDER"},
                        {"order_id", entry.order_id}
                    });
                    continue;
                }
                if (entry.reject_reason != OrdRejectReason::NONE) {
                    results.push_back({
                        {"success", false},
                        {"failure_reason", to_string(entry.reject_reason)},
                        {"order_id", entry.order_id}
                    });
                } else {
                    results.push_back({
                        {"success", true},
                        {"failure_reason", ""},
                        {"order_id", entry.order_id}
                    });
                    order_id_to_symbol_.erase(entry.order_id);
                }
            }

            auto rejected_count = static_cast<int>(std::count_if(entries.begin(), entries.end(),
                [](const CancelEntry& e) { return e.submitted && e.reject_reason != OrdRejectReason::NONE; }));

            res->writeStatus("200 OK")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({{"results", results}}).dump());

            LOG_INFO("batch_cancel completed: user={}, total={}, rejected={}",
                     user_id, entries.size(), rejected_count);

        } catch (const json::parse_error& e) {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_REQUEST"},
                    {"message", std::string("Invalid JSON: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON parse error in POST /orders/batch_cancel: {}", e.what());
        } catch (const json::exception& e) {
            res->writeStatus("400 Bad Request")
                ->writeHeader("Content-Type", "application/json")
                ->end(json({
                    {"error", "INVALID_ARGUMENT"},
                    {"message", std::string("Missing or invalid field: ") + e.what()}
                }).dump());
            LOG_ERROR("JSON field error in POST /orders/batch_cancel: {}", e.what());
        } catch (const std::exception& e) {
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

void CoinbaseRestClientManager::handle_edit_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
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
            std::memset(request->symbol, 0, sizeof(request->symbol));
            std::memcpy(request->symbol, symbol.c_str(), std::min(sizeof(request->symbol), symbol.size()));
            request->msg_type = MessageType::ORDER_REPLACE_REQUEST;

            std::memset(&request->modify_order, 0, sizeof(request->modify_order));
            std::memcpy(request->modify_order.user_id, user_id.c_str(),
                        std::min(sizeof(request->modify_order.user_id), user_id.size()));
            std::memcpy(request->modify_order.order_id, order_id.c_str(),
                        std::min(sizeof(request->modify_order.order_id), order_id.size()));
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

void CoinbaseRestClientManager::handle_get_transaction_summary(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
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