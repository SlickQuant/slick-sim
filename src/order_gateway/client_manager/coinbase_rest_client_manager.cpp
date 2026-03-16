#include "coinbase_rest_client_manager.hpp"

using namespace slick::sim::order_gateway::coinbase;

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

    auto iter = orders_.find(user_id);
    if (iter != orders_.end()) {
        for (auto &kvp : iter->second) {
            auto &order = kvp.second;
            response["/orders"_json_pointer].emplace_back(to_json(order));
        }
    }
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

                LOG_ERROR("Order rejected: user={}, client_order_id={}, reason=Unsupported order_configuration type",
                            user_id, client_order_id);
                return;
            }

            Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;

            // Reserve space in request queue
            auto request_index = request_queue_.reserve();
            auto* request = request_queue_[request_index];

            // Populate Request struct
            request->time_stamp = std::chrono::system_clock::now().time_since_epoch().count();
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
                        assert(matched_response->order_status == OrderStatus::PENDING_NEW);
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

                // Store order in local cache for historical queries
                Order order;
                order.user_id = matched_response->user_id;
                order.client_order_id = matched_response->client_order_id;
                order.order_id = matched_response->order_id;
                order.symbol = product_id;
                order.side = side;
                order.type = order_type;
                order.time_in_force = tif;
                order.price = matched_response->price;
                order.quantity = matched_response->qty;
                order.leaves_quantity = matched_response->leaves_qty;
                order.filled_quantity = matched_response->cum_qty;
                order.post_only = post_only;
                order.status = matched_response->order_status;
                orders_[user_id][std::string(matched_response->order_id)] = order;

                res->writeStatus("200 OK")
                    ->writeHeader("Content-Type", "application/json")
                    ->end(response_json.dump());

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