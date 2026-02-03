#include "client_manager.hpp"
#include <uwebsockets/App.h>
#include <slick/net/http.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include "coinbase_common.hpp"

struct us_listen_socket_t;
using Http = slick::net::Http;
using json = nlohmann::json;

namespace slick::sim::order_gateway::coinbase {

class CoinbaseRestClientManager : public ClientManager {
    std::thread thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uint32_t port_ = 3000;
    std::string products_;
    std::unordered_map<std::string, std::string> product_by_id_;
    std::unordered_map<std::string, std::unordered_map<std::string, Order>> orders_;
    std::unordered_multimap<std::string, Fill> order_fills_;

public:
    CoinbaseRestClientManager(const json& config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<OrderResponse> &response_queue)
        : ClientManager(config, request_queue, response_queue)
        , port_(config_.value("port", 3000))
    {
        auto rsp = Http::get("https://api.coinbase.com/api/v3/brokerage/market/products");
        if (rsp.is_ok()) {
            products_ = rsp.result_text;
            auto products = json::parse(products_);
            for (auto &prod : products["products"]) {
                product_by_id_[prod["product_id"]] = prod.dump();
            }
        }
        else {
            LOG_WARN("Failed to get products. error: {}", rsp.result_text);
        }
    }
    ~CoinbaseRestClientManager() override {
        stop();
    }

    void start() override {
        thread_ = std::move(std::thread([this]() {
            uWS::App().get("/api/v3/brokerage/market/products", [this](auto *res, auto *req) {
                res->writeHeader("Content-Type", "application/json")->end(products_);
            }).get("/api/v3/brokerage/products", [this](auto *res, auto *req) {
                res->writeHeader("Content-Type", "application/json")->end(products_);
            }).get("/api/v3/brokerage/products/:product_id", [this](auto *res, auto *req) {
                std::string_view product_id = req->getParameter("product_id");
                LOG_INFO("Fetching product: {}", product_id);
                auto iter = product_by_id_.find(std::string(product_id));
                if (iter != product_by_id_.end()) {
                    res->end(product_by_id_[std::string(product_id)]);
                }
                else {
                    res->writeStatus("400 Bad Request")
                       ->writeHeader("Content-Type", "application/json")
                       ->end(R"({"error":"INVALID_ARGUMENT","error_details":"valid product_id is required","message":"valid product_id is required"})");
                }
            }).get("/api/v3/brokerage/market/products/:product_id", [this](auto *res, auto *req) {
                std::string_view product_id = req->getParameter("product_id");
                auto iter = product_by_id_.find(std::string(product_id));
                if (iter != product_by_id_.end()) {
                    res->end(product_by_id_[std::string(product_id)]);
                }
                else {
                    res->writeStatus("400 Bad Request")
                       ->writeHeader("Content-Type", "application/json")
                       ->end(R"({"error":"INVALID_ARGUMENT","error_details":"valid product_id is required","message":"valid product_id is required"})");
                }
            }).get("/api/v3/brokerage/orders/historical/batch", [this](auto *res, auto *req) {
                // Get the Authorization header
                std::string_view auth_header = req->getHeader("Authorization");
                
                // For Coinbase, it's typically: "Bearer <JWT_TOKEN>"
                if (auth_header.empty()) {
                    res->writeStatus("401 Unauthorized")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"UNAUTHORIZED","message":"Authorization header required"})");
                    return;
                }
                
                // Extract the token (remove "Bearer " prefix)
                std::string token;
                if (auth_header.starts_with("Bearer ")) {
                    token = std::string(auth_header.substr(7));
                } else {
                    res->writeStatus("401 Unauthorized")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"UNAUTHORIZED","message":"Invalid auth token"})");
                    return;
                }

                try {
                    auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);

                    // Extract 'sub' claim
                    std::string sub = decoded.get_subject();
                    std::string user_id = sub.substr(sub.rfind('/') + 1);
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

                } catch (const std::exception& e) {
                    res->writeStatus("401 Unauthorized")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(R"({"error":"UNAUTHORIZED","message":"Invalid token"})");
                    return;
                }
            }).listen(port_, [this](auto *listen_socket) {
                listen_socket_ = listen_socket;
                if (listen_socket) {
                    LOG_INFO("Coinbase REST OrderGateway started on port {}", port_);
                } else {
                    LOG_ERROR("Failed to listen to port");
                }
            }).run();
        }));
    }

    void stop() override {
        if (listen_socket_) {
            us_listen_socket_close(0, listen_socket_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }
};

}