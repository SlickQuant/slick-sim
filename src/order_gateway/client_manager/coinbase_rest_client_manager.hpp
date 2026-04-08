#include "client_manager.hpp"
#include <uwebsockets/App.h>
#include <slick/net/http.hpp>
#include <nlohmann/json.hpp>
// #define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <unordered_map>
#include <string>
#include <chrono>
#include <thread>
#include <tuple>
#include <memory>
#include "coinbase_common.hpp"

struct us_listen_socket_t;
using Http = slick::net::Http;
using json = nlohmann::json;

namespace slick::sim::order_gateway::coinbase {

// Per-request data for HTTP POST handling
struct PostRequestData {
    std::string body_buffer;
    std::string user_id;
};

class CoinbaseRestClientManager : public ClientManager {
    std::thread thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uint32_t port_ = 3000;
    std::string products_;
    std::unordered_map<std::string, std::string> product_by_id_;

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

    std::tuple<bool, std::string, std::string> authenticate(uWS::HttpRequest *req);

    void start() override;

    void stop() override {
        if (listen_socket_) {
            us_listen_socket_close(0, listen_socket_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void setup_routes(uWS::App &app);
    void handle_get_product(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_get_orders(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_create_order(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
};

}