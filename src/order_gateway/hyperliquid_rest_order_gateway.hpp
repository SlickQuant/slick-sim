#pragma once

#include "rest_ws_order_gateway.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <hyperliquid/info.hpp>

struct us_listen_socket_t;
using json = nlohmann::json;

namespace hyperliquid { class Info; }

namespace slick::sim::order_gateway {

struct PostRequestData {
    std::string body_buffer;
    std::string user_id;
};

class HyperliquidRestOrderGateway : public RestWsOrderGateway {
    // std::thread thread_;
    // us_listen_socket_t* listen_socket_ = nullptr;
    // uWS::Loop* loop_ = nullptr;
    uint32_t port_ = 4003;
    std::string base_url_;
    std::string default_wallet_;

    // asset index -> coin name (e.g. 3 -> "ETH")
    // std::unordered_map<int, std::string> asset_to_coin_;
    // coin name -> asset index
    // std::unordered_map<std::string, int> coin_to_asset_;
    hyperliquid::Info info_;

public:
    HyperliquidRestOrderGateway(const json& config,
                                 slick::queue<Request>& request_queue,
                                 slick::queue<OrderResponse>& response_queue);
    ~HyperliquidRestOrderGateway() override = default;

    std::string_view type() const noexcept override { return "REST"; }

    // void start() override;
    // void stop() override {
    //     if (listen_socket_ && loop_) {
    //         // Closing the listen socket must happen on the loop's own thread;
    //         // doing it cross-thread races with the loop's poll handles.
    //         loop_->defer([this]() {
    //             if (listen_socket_) {
    //                 us_listen_socket_close(0, listen_socket_);
    //                 listen_socket_ = nullptr;
    //             }
    //         });
    //     }
    //     if (thread_.joinable()) thread_.join();
    // }

private:
    // void load_meta();
    std::string extract_wallet(const json& body, uWS::HttpRequest* req);
    std::string coin_for_asset(int asset_index) const;
    void setup_routes(uWS::App& app) override;

    void handle_exchange(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_info(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);

    void process_order_action(uWS::HttpResponse<false>* res,
                              const json& action, const std::string& user_id);
    void process_cancel_action(uWS::HttpResponse<false>* res,
                               const json& action, const std::string& user_id);
    void process_cancel_by_cloid_action(uWS::HttpResponse<false>* res,
                                        const json& action, const std::string& user_id);
    void process_batch_modify_action(uWS::HttpResponse<false>* res,
                                     const json& action, const std::string& user_id);
};

}   // end namespace slick::sim::order_gateway::hyperliquid_hl
