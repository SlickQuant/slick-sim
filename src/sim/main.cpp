#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <slick/logger.hpp>
// #include <slick/socket/tcp_server.h>
// #include <order_gateway/order_gateway.hpp>
// #include <order_gateway/message_parser.hpp>
#include <exchange/exch_coinbase.hpp>
#include <exchange/exch_hyperliquid.hpp>
#include <common/messages.hpp>
// #include <market_data_publisher/market_data_publisher.hpp>
#include <slick/net/logging.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <csignal>
#include <fstream>

using namespace slick::sim;
using namespace slick::sim::order_gateway;
using namespace slick::sim::md_publisher;
using json = nlohmann::json;
using Exchange = slick::sim::exch::Exchange;

namespace {
    std::atomic_bool run{true};

    extern "C" void _signal_handler(int /* signum */) {
        run.store(false, std::memory_order_release);
        // exit(signum); // Terminate the program
    }
}

// void process_orders(OrderGateway& gateway) {
//     auto& order_queue = gateway.get_order_request_queue();
//     uint64_t read_index = 0;
    
//     LOG_INFO("Multi-protocol order processor started...");
    
//     while (true) {
//         auto [order_ptr, count] = order_queue.read(read_index);
//         if (order_ptr && count > 0) {
//             const Order& order = *order_ptr;
            
//             LOG_INFO("Processing order: ClientOrderID={}, Symbol={}, Side={}, Qty={}, Price={} from client {}",
//                      order.client_order_id, order.symbol, static_cast<char>(order.side),
//                      order.quantity, order.price, order.client_id);
            
//             // Simulate order processing and send execution report back to the specific client
//             OrderResponse response;
//             response.client_order_id = order.client_order_id;
//             response.exec_id = "EXEC" + std::to_string(read_index);
//             response.order_status = OrderStatus::Filled;
//             response.price = order.price;
//             response.quantity = order.quantity;
//             response.cum_qty = order.quantity;
//             response.leaves_qty = 0;
            
//             gateway.send_execution_report(response, order.client_id);
//         } else {
//             std::this_thread::sleep_for(std::chrono::milliseconds(1));
//         }
//     }
// }

void print_protocol_info() {
    LOG_INFO("=== Supported Protocols ===");
    LOG_INFO("1. FIX 4.4 - Financial Information Exchange (Traditional trading)");
    LOG_INFO("2. SBE - Simple Binary Encoding (CME-style low-latency)"); 
    LOG_INFO("3. JSON - JavaScript Object Notation (Crypto exchanges)");
    LOG_INFO("");
    LOG_INFO("=== Connection Examples ===");
    LOG_INFO("FIX: Connect and send: 8=FIX.4.4|9=...|35=D|...|10=123|");
    LOG_INFO("JSON: Connect and send: {{\"type\":\"newOrder\",\"symbol\":\"BTCUSD\",\"side\":\"buy\",...}}");
    LOG_INFO("SBE: Connect and send binary SBE-encoded messages");
    LOG_INFO("");
}

// void print_client_status(OrderGateway& gateway) {
//     auto protocols = gateway.get_client_protocols();
//     if (!protocols.empty()) {
//         LOG_INFO("=== Connected Clients ===");
//         for (const auto& [client_id, protocol] : protocols) {
//             std::string protocol_name;
//             switch (protocol) {
//                 case ProtocolType::FIX: protocol_name = "FIX"; break;
//                 case ProtocolType::SBE: protocol_name = "SBE"; break;
//                 case ProtocolType::JSON: protocol_name = "JSON"; break;
//                 default: protocol_name = "Unknown"; break;
//             }
//             LOG_INFO("Client {}: Using {} protocol", client_id, protocol_name);
//         }
//     } else {
//         LOG_INFO("No clients connected");
//     }
// }

int main(int argc, char* argv[]) {

    std::signal(SIGINT, _signal_handler);

    auto &logger = slick::logger::Logger::instance();
    logger.clear_sinks();
#ifdef DEBUG
    logger.add_console_sink(true, true);
    // logger.set_level(slick::logger::LogLevel::L_TRACE);
    logger.set_level(slick::logger::LogLevel::L_DEBUG);
#else
    logger.set_level(slick::logger::LogLevel::L_INFO);
#endif
    logger.add_file_sink("logs/slick-sim.log");
    logger.init(65535, 16777216);

    slick::net::set_log_handler(
        [&logger](slick::net::LogLevel level, const char* format_text, std::format_args args) {
            logger.log(static_cast<slick::logger::LogLevel>(level), format_text, std::move(args));
        },
        [&logger]() -> slick::net::LogLevel {
            return static_cast<slick::net::LogLevel>(logger.get_level());
        }
    );

    std::string config_file = "slick-sim.json";
    if (argc > 1) {
        config_file = argv[1];
    }

    if (!std::filesystem::exists(config_file)) {
        std::cout << "Missing configuration JSON file. Usange\n" << "    slick-sim [config_file]\n"
            << "    options:\n"
            << "      config_file: config JSON file. Default to slick-sim.json";
        LOG_ERROR("Missing configuration JSON file. Usage\n    slick-sim [config_file]\n    options:\n      config_file: config JSON file. Default to slick-sim.json");
        return EXIT_FAILURE;
    }

    std::fstream f(config_file);
    json config;
    try
    {
        config = json::parse(f);
    }
    catch (const json::parse_error& ex)
    {
        std::cerr << "Parsing failed: \n" << ex.byte;
        LOG_ERROR("Parsing failed: \n{}", ex.byte);
        return EXIT_FAILURE;
    }

    // if (!config.contains("order_gateway")) {
    //     std::cerr << "Missing order_gateway config";
    //     LOG_ERROR("Missing order_gateway config");
    //     return EXIT_FAILURE;
    // }

    if (!config.contains("exchanges")) {
        std::cerr << "Missing exchanges config";
        LOG_ERROR("Missing exchanges config");
        return EXIT_FAILURE;
    }

    logger.set_level(slick::logger::to_log_level(config.value("log_level", "info")));

    // print_protocol_info();

    // auto order_request_queue_size = config.value("order_request_queue_size", 1048576);
    // auto response_queue_size = config.value("response_queue_size", 1048576);
    // auto md_queue_size = config.value("md_queue_size", 16777216);

    // slick::queue<Request> request_queue(order_request_queue_size);
    // slick::queue<OrderResponse> response_queue(response_queue_size);
    // slick::queue<uint8_t> md_update_queue(md_queue_size);

    // Create OrderGateway with auto-detection enabled (default)
    // OrderGateway gateway(config["order_gateway"], request_queue, response_queue, ProtocolType::FIX);
    // gateway.enable_auto_protocol_detection(true);
    
    // Set order callback
    // gateway.set_order_callback([&gateway](const Order& order) {
    //     LOG_INFO("Order received via callback: {} from client {} using auto-detected protocol", 
    //             order.client_order_id, order.client_id);
        
    //     // Print current client status
    //     print_client_status(gateway);
    // });

    // Example: Set specific protocol for certain clients (optional)
    // gateway.set_client_protocol(1, ProtocolType::FIX);
    // gateway.set_client_protocol(2, ProtocolType::JSON);
    // gateway.set_client_protocol(3, ProtocolType::SBE);

    // Start the gateway
    // gateway.start();

    auto &exch_coinfig = config["exchanges"];
    std::vector<std::unique_ptr<Exchange>> exchanges;
    for (auto it = exch_coinfig.begin(); it != exch_coinfig.end(); ++it)
    {
        if (it.key() == "coinbase") {
            auto enabled = it.value().value("enabled", true);
            if (enabled) {
                exchanges.emplace_back(std::make_unique<slick::sim::exch::CoinbaseExchange>(it.value()));
                exchanges.back()->start();
            }
        }
        else if (it.key() == "hyperliquid") {
            auto enabled = it.value().value("enabled", true);
            if (enabled) {
                exchanges.emplace_back(std::make_unique<slick::sim::exch::HyperliquidExchange>(it.value()));
                exchanges.back()->start();
            }
        } else {
            exchanges.emplace_back(std::make_unique<Exchange>(to_venue(it.key()), it.value()));
            exchanges.back()->start();
        }
    }

    // MarketDataPublisher market_data_publisher(config["md_publisher"], request_queue, md_update_queue);
    // market_data_publisher.start();

    // LOG_INFO("Multi-protocol OrderGateway started on port {}", config.port);
    // LOG_INFO("Auto-protocol detection: ENABLED");
    // LOG_INFO("Clients can connect using FIX, JSON, or SBE protocols");

    // Start order processing thread
    // std::thread order_processor(process_orders, std::ref(gateway));
    // order_processor.detach();

    // // Status monitoring thread
    // std::thread status_monitor([&gateway]() {
    //     while (true) {
    //         std::this_thread::sleep_for(std::chrono::seconds(30));
    //         size_t client_count = gateway.get_active_client_count();
    //         if (client_count > 0) {
    //             LOG_INFO("Active clients: {}", client_count);
    //             print_client_status(gateway);
    //         }
    //     }
    // });
    // status_monitor.detach();

    // Start exchanges


    // Wait for user input to stop
    LOG_INFO("Press Ctrl+C to stop the server...");
    while (run.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto &exch : exchanges) {
        exch->stop();
    }

    LOG_INFO("ExchangeSimulator stopped");
    return EXIT_SUCCESS;
}
