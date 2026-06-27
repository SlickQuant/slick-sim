#include "rest_ws_order_gateway.hpp"
#include <slick/net/http.hpp>
#include <nlohmann/json.hpp>
// #define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <unordered_map>
#include <string>
#include <chrono>
#include <tuple>
#include <memory>

struct us_listen_socket_t;
using json = nlohmann::json;

namespace slick::sim::order_gateway {

class CoinbaseRestOrderGateway : public RestWsOrderGateway {
    // std::thread thread_;
    // us_listen_socket_t* listen_socket_ = nullptr;
    // uint32_t port_ = 3000;
    std::string products_;
    std::unordered_map<std::string, std::string> product_by_id_;

public:
    CoinbaseRestOrderGateway(const json& config, slick::queue<Request> &request_queue, slick::queue<OrderResponse> &response_queue);
    ~CoinbaseRestOrderGateway() override = default;

    std::string_view type() const noexcept override { return "REST"; }

    std::tuple<bool, std::string, std::string> authenticate(uWS::HttpRequest *req);

    // void start() override;

    // void stop() override {
    //     if (listen_socket_) {
    //         us_listen_socket_close(0, listen_socket_);
    //     }
    //     if (thread_.joinable()) {
    //         thread_.join();
    //     }
    // }

private:
    struct OrderValidationResult {
        bool valid = true;
        std::string error_type;
        std::string error_details;
    };

    std::unordered_map<std::string, std::string> order_id_to_symbol_;

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