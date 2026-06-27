#include <slick/logger.hpp>
#include "rest_ws_order_gateway.hpp"

namespace slick::sim::order_gateway {

void RestWsOrderGateway::start() {
    thread_ = std::thread([this]() {
        auto app = uWS::App();
        setup_routes(app);
        app.listen(port_, [this](auto* listen_socket) {
            listen_socket_ = listen_socket;
            if (listen_socket) {
                loop_ = uWS::Loop::get();
                on_listen_socket(listen_socket);
                LOG_INFO("{} {} OrderGateway started on port {}", to_string(venue_), type(), port_);
            } else {
                LOG_ERROR("{} {} OrderGateway failed to listen on port {}", to_string(venue_), type(), port_);
            }
        }).run();
    });
}

}