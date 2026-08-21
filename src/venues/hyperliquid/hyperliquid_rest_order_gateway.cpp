#include "hyperliquid_rest_order_gateway.hpp"
#include <slick/logger.hpp>
#include <slick/net/http.hpp>
#include "hyperliquid_info_proxy.hpp"
#include <utils/fixed_string.hpp>
#include <utils/timestamp.hpp>
#include <thread>
#include <chrono>
#include <cstring>
#include <format>

using namespace slick::sim::order_gateway;
using namespace slick::sim;
using namespace slick::sim::utils;
using Http = slick::net::Http;

HyperliquidRestOrderGateway::HyperliquidRestOrderGateway(
    const json& config,
    slick::queue<Request>& request_queue,
    slick::queue<OrderResponse>& response_queue)
    : RestWsOrderGateway(Venue::HYPERLIQUID, request_queue, response_queue, config.value("port", 4003))
    , base_url_(config.value("base_url", "https://api.hyperliquid.xyz"))
    , default_wallet_(config.value("default_wallet", "0x0000000000000000000000000000000000000001"))
    , info_(base_url_, false)
{
    try {
        info_.load_meta();
        LOG_INFO("HyperliquidREST: loaded {} assets", info_.coin_to_asset.size());
    } catch (const std::exception& e) {
        LOG_WARN("HyperliquidREST: exception loading meta: {}", e.what());
    }
}

// void HyperliquidRestOrderGateway::load_meta() {
//     try {
//         json meta_req = {{"type", "meta"}};
//         auto rsp = Http::post(base_url_ + "/info", meta_req.dump(), {{"Content-Type", "application/json"}});
//         if (!rsp.is_ok()) {
//             LOG_WARN("HyperliquidREST: failed to load meta: {}", rsp.result_text);
//             return;
//         }
//         auto meta = json::parse(rsp.result_text);
//         if (!meta.contains("universe")) return;
//         int idx = 0;
//         for (const auto& asset : meta["universe"]) {
//             std::string name = asset.value("name", "");
//             if (!name.empty()) {
//                 asset_to_coin_[idx] = name;
//                 coin_to_asset_[name] = idx;
//             }
//             ++idx;
//         }
//         LOG_INFO("HyperliquidREST: loaded {} assets", asset_to_coin_.size());
//     } catch (const std::exception& e) {
//         LOG_WARN("HyperliquidREST: exception loading meta: {}", e.what());
//     }
// }

std::string HyperliquidRestOrderGateway::coin_for_asset(int asset_index) const {
    // auto it = asset_to_coin_.find(asset_index);
    // if (it != asset_to_coin_.end()) return it->second;
    // return {};
    auto it = info_.name_to_coin.find("@" + std::to_string(asset_index));
    if (it != info_.name_to_coin.end()) {
        return it->second;
    }
    return {};
}

std::string HyperliquidRestOrderGateway::extract_wallet(const json& body, uWS::HttpRequest* req) {
    // 1. X-Wallet-Address header
    std::string_view hdr = req->getHeader("x-wallet-address");
    if (!hdr.empty()) return std::string(hdr);
    // 2. vaultAddress in body
    if (body.contains("vaultAddress") && !body["vaultAddress"].is_null()) {
        return body["vaultAddress"].get<std::string>();
    }
    // 3. Fallback: configured default
    return default_wallet_;
}

// void HyperliquidRestOrderGateway::start() {
//     thread_ = std::thread([this]() {
//         auto app = uWS::App();
//         setup_routes(app);
//         app.listen(port_, [this](auto* listen_socket) {
//             listen_socket_ = listen_socket;
//             if (listen_socket) {
//                 loop_ = uWS::Loop::get();
//                 LOG_INFO("HyperliquidREST started on port {}", port_);
//             } else {
//                 LOG_ERROR("HyperliquidREST failed to listen on port {}", port_);
//             }
//         }).run();
//     });
// }

void HyperliquidRestOrderGateway::setup_routes(uWS::App& app) {
    app.post("/exchange", [this](auto* res, auto* req) {
        handle_exchange(res, req);
    }).post("/info", [this](auto* res, auto* req) {
        handle_info(res, req);
    }).get("/info", [this](auto* res, auto* req) {
        handle_info(res, req);
    });
}

void HyperliquidRestOrderGateway::handle_exchange(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req)
{
    auto request_data = std::make_shared<PostRequestData>();

    res->onAborted([request_data]() {
        LOG_WARN("HyperliquidREST: POST /exchange aborted");
    });

    res->onData([this, res, req, request_data](std::string_view chunk, bool is_last) mutable {
        request_data->body_buffer.append(chunk);
        if (!is_last) return;

        try {
            json body = json::parse(request_data->body_buffer);
            std::string user_id = extract_wallet(body, req);

            if (!body.contains("action")) {
                res->writeStatus("400 Bad Request")
                   ->writeHeader("Content-Type", "application/json")
                   ->end(R"({"status":"err","response":"missing action"})");
                return;
            }
            const auto& action = body["action"];
            std::string type = action.value("type", "");

            if (type == "order") {
                process_order_action(res, action, user_id);
            } else if (type == "cancel") {
                process_cancel_action(res, action, user_id);
            } else if (type == "cancelByCloid") {
                process_cancel_by_cloid_action(res, action, user_id);
            } else if (type == "batchModify") {
                process_batch_modify_action(res, action, user_id);
            } else {
                res->writeStatus("400 Bad Request")
                   ->writeHeader("Content-Type", "application/json")
                   ->end(std::format(R"({{"status":"err","response":"unsupported action type: {}"}})", type));
            }
        } catch (const std::exception& e) {
            res->writeStatus("400 Bad Request")
               ->writeHeader("Content-Type", "application/json")
               ->end(std::format(R"({{"status":"err","response":"parse error: {}"}})", e.what()));
        }
    });
}

void HyperliquidRestOrderGateway::handle_info(
    uWS::HttpResponse<false>* res, uWS::HttpRequest*)
{
    proxy_hyperliquid_info_request(res, base_url_);
}

void HyperliquidRestOrderGateway::process_order_action(
    uWS::HttpResponse<false>* res, const json& action, const std::string& user_id)
{
    if (!action.contains("orders") || !action["orders"].is_array()) {
        res->writeStatus("400 Bad Request")
           ->writeHeader("Content-Type", "application/json")
           ->end(R"({"status":"err","response":"missing orders array"})");
        return;
    }

    json statuses = json::array();

    for (const auto& order : action["orders"]) {
        int asset_idx = order["a"].is_number() ? order["a"].get<int>() : -1;
        bool is_buy   = order.value("b", true);
        std::string price_str = order.value("p", "0");
        std::string size_str  = order.value("s", "0");
        [[maybe_unused]] bool reduce_only = order.value("r", false);

        std::string coin = coin_for_asset(asset_idx);
        if (coin.empty()) {
            // Fall back: allow coin name as string in "a" if asset map is empty
            if (order["a"].is_string()) {
                coin = order["a"].get<std::string>();
            } else {
                statuses.push_back({{"error", std::format("unknown asset index {}", asset_idx)}});
                continue;
            }
        }

        double price_d = std::stod(price_str);
        double size_d  = std::stod(size_str);
        auto price = to_price_t(price_d);
        auto qty   = to_qty_t(size_d);

        // Parse order type
        OrderType order_type = OrderType::LIMIT;
        TimeInForce tif      = TimeInForce::GOOD_TILL_CANCEL;
        if (order.contains("t")) {
            const auto& t = order["t"];
            if (t.contains("limit")) {
                std::string tif_str = t["limit"].value("tif", "Gtc");
                if (tif_str == "Ioc")      tif = TimeInForce::IMMEDIATE_OR_CANCEL;
                else if (tif_str == "Alo") tif = TimeInForce::GOOD_TILL_CANCEL;  // post-only
                // Gtc is default
            } else if (t.contains("trigger")) {
                // Simplified: treat triggers as limit orders for the sim
                order_type = OrderType::LIMIT;
            }
        }

        // Generate a client order id from cloid field or nonce-based
        std::string cloid;
        if (order.contains("c") && order["c"].is_string()) {
            cloid = order["c"].get<std::string>();
        } else {
            cloid = std::format("hl_{}", get_current_time_ns());
        }

        // Captured before the request is queued, not after. initial_reading_index()
        // returns the response queue's current write cursor, and the exchange thread is
        // a hot spin loop - it can consume the request and publish PENDING_NEW before
        // this thread runs its next instruction. Reading that cursor after publishing
        // therefore starts the poll *past* our own response, and the order comes back
        // to the client as a timeout while it is resting, or already filled, in the book.
        uint64_t response_read_index = response_queue_.initial_reading_index();

        auto req_index = request_queue_.reserve();
        auto* request  = request_queue_[req_index];
        request->time_stamp = get_current_time_ns();
        utils::copy_wire_field(request->symbol, coin);
        request->msg_type = MessageType::NEW_ORDER_SINGLE;

        utils::copy_wire_field(request->add_order.user_id, user_id);
        utils::copy_wire_field(request->add_order.client_order_id, cloid);

        request->add_order.side           = is_buy ? Side::BUY : Side::SELL;
        request->add_order.type           = order_type;
        request->add_order.time_in_force  = tif;
        request->add_order.price          = price;
        request->add_order.qty            = qty;
        request->add_order.take_profit_price = NULL_PRICE;
        request->add_order.stop_loss_price  = NULL_PRICE;
        request->add_order.expire_time      = 0;

        // Save stored uid/cloid (potentially truncated) before publish for response matching
        std::string stored_uid(request->add_order.user_id,
            strnlen(request->add_order.user_id, sizeof(request->add_order.user_id)));
        std::string stored_cloid(request->add_order.client_order_id,
            strnlen(request->add_order.client_order_id, sizeof(request->add_order.client_order_id)));

        request_queue_.publish(req_index);

        LOG_INFO("HyperliquidREST: order queued user={} coin={} side={} px={} sz={} cloid={}",
                 user_id, coin, is_buy ? "B" : "A", price_str, size_str, cloid);

        // Poll for response
        constexpr auto timeout    = std::chrono::milliseconds(5000);
        constexpr auto poll_delay = std::chrono::microseconds(100);
        auto deadline = std::chrono::steady_clock::now() + timeout;
        OrderResponse* resp = nullptr;

        while (std::chrono::steady_clock::now() < deadline) {
            auto [data, sz] = response_queue_.read(response_read_index);
            if (data &&
                std::string_view(data->user_id) == stored_uid &&
                std::string_view(data->client_order_id) == stored_cloid)
            {
                resp = data;
                break;
            }
            std::this_thread::sleep_for(poll_delay);
        }

        if (!resp) {
            statuses.push_back({{"error", "timeout"}});
            continue;
        }

        if (resp->order_status == OrderStatus::REJECTED) {
            statuses.push_back({{"error", to_string(resp->reject_reason)}});
        } else {
            std::string oid_str(resp->order_id,
                strnlen(resp->order_id, sizeof(resp->order_id)));
            statuses.push_back({{"resting", {{"oid", oid_str.empty() ? 0 : std::stoll(oid_str)}}}});
        }
    }

    json response = {
        {"status", "ok"},
        {"response", {
            {"type", "order"},
            {"data", {{"statuses", statuses}}}
        }}
    };
    res->writeHeader("Content-Type", "application/json")->end(response.dump());
}

void HyperliquidRestOrderGateway::process_cancel_action(
    uWS::HttpResponse<false>* res, const json& action, const std::string& user_id)
{
    if (!action.contains("cancels") || !action["cancels"].is_array()) {
        res->writeStatus("400 Bad Request")
           ->writeHeader("Content-Type", "application/json")
           ->end(R"({"status":"err","response":"missing cancels array"})");
        return;
    }

    json statuses = json::array();

    for (const auto& cancel : action["cancels"]) {
        int asset_idx = cancel["a"].is_number() ? cancel["a"].get<int>() : -1;
        int64_t oid   = cancel["o"].is_number() ? cancel["o"].get<int64_t>() : int64_t{0};

        std::string coin = coin_for_asset(asset_idx);
        if (coin.empty() && cancel["a"].is_string()) {
            coin = cancel["a"].get<std::string>();
        }
        if (coin.empty()) {
            statuses.push_back({{"error", "unknown asset"}});
            continue;
        }

        std::string oid_str = std::to_string(oid);

        auto req_index = request_queue_.reserve();
        auto* request  = request_queue_[req_index];
        request->time_stamp = get_current_time_ns();
        utils::copy_wire_field(request->symbol, coin);
        request->msg_type = MessageType::ORDER_CANCEL_REQUEST;

        utils::copy_wire_field(request->cancel_order.user_id, user_id);
        utils::copy_wire_field(request->cancel_order.order_id, oid_str);

        request_queue_.publish(req_index);
        statuses.push_back({{"success", oid}});
    }

    json response = {
        {"status", "ok"},
        {"response", {
            {"type", "cancel"},
            {"data", {{"statuses", statuses}}}
        }}
    };
    res->writeHeader("Content-Type", "application/json")->end(response.dump());
}

void HyperliquidRestOrderGateway::process_cancel_by_cloid_action(
    uWS::HttpResponse<false>* res, const json& action, const std::string& user_id)
{
    if (!action.contains("cancels") || !action["cancels"].is_array()) {
        res->writeStatus("400 Bad Request")
           ->writeHeader("Content-Type", "application/json")
           ->end(R"({"status":"err","response":"missing cancels array"})");
        return;
    }

    json statuses = json::array();

    for (const auto& cancel : action["cancels"]) {
        int asset_idx  = cancel["a"].is_number() ? cancel["a"].get<int>() : -1;
        std::string cloid = cancel.value("c", "");

        std::string coin = coin_for_asset(asset_idx);
        if (coin.empty() && cancel["a"].is_string()) {
            coin = cancel["a"].get<std::string>();
        }
        if (coin.empty() || cloid.empty()) {
            statuses.push_back({{"error", "invalid cancel request"}});
            continue;
        }

        auto req_index = request_queue_.reserve();
        auto* request  = request_queue_[req_index];
        request->time_stamp = get_current_time_ns();
        utils::copy_wire_field(request->symbol, coin);
        request->msg_type = MessageType::ORDER_CANCEL_REQUEST;

        utils::copy_wire_field(request->cancel_order.user_id, user_id);
        utils::copy_wire_field(request->cancel_order.client_order_id, cloid);

        request_queue_.publish(req_index);
        statuses.push_back({{"success", cloid}});
    }

    json response = {
        {"status", "ok"},
        {"response", {
            {"type", "cancel"},
            {"data", {{"statuses", statuses}}}
        }}
    };
    res->writeHeader("Content-Type", "application/json")->end(response.dump());
}

void HyperliquidRestOrderGateway::process_batch_modify_action(
    uWS::HttpResponse<false>* res, const json& action, const std::string& user_id)
{
    if (!action.contains("modifies") || !action["modifies"].is_array()) {
        res->writeStatus("400 Bad Request")
           ->writeHeader("Content-Type", "application/json")
           ->end(R"({"status":"err","response":"missing modifies array"})");
        return;
    }

    json statuses = json::array();

    for (const auto& mod : action["modifies"]) {
        int64_t oid = mod.value("oid", int64_t{0});
        if (!mod.contains("order")) {
            statuses.push_back({{"error", "missing order in modify"}});
            continue;
        }
        const auto& order = mod["order"];
        int asset_idx = order["a"].is_number() ? order["a"].get<int>() : -1;
        std::string price_str = order.value("p", "0");
        std::string size_str  = order.value("s", "0");

        std::string coin = coin_for_asset(asset_idx);
        if (coin.empty() && order.contains("a") && order["a"].is_string()) {
            coin = order["a"].get<std::string>();
        }
        if (coin.empty()) {
            statuses.push_back({{"error", "unknown asset"}});
            continue;
        }

        std::string oid_str = std::to_string(oid);
        auto new_price = to_price_t(std::stod(price_str));
        auto new_qty   = to_qty_t(std::stod(size_str));

        auto req_index = request_queue_.reserve();
        auto* request  = request_queue_[req_index];
        request->time_stamp = get_current_time_ns();
        utils::copy_wire_field(request->symbol, coin);
        request->msg_type = MessageType::ORDER_REPLACE_REQUEST;

        utils::copy_wire_field(request->modify_order.user_id, user_id);
        utils::copy_wire_field(request->modify_order.order_id, oid_str);
        request->modify_order.new_price = new_price;
        request->modify_order.new_qty   = new_qty;

        request_queue_.publish(req_index);
        statuses.push_back({{"success", oid}});
    }

    json response = {
        {"status", "ok"},
        {"response", {
            {"type", "batchModify"},
            {"data", {{"statuses", statuses}}}
        }}
    };
    res->writeHeader("Content-Type", "application/json")->end(response.dump());
}
