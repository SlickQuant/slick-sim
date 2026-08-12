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

namespace
{
    std::atomic_bool run{true};

    extern "C" void _signal_handler(int /* signum */)
    {
        run.store(false, std::memory_order_release);
        // exit(signum); // Terminate the program
    }
}

int main(int argc, char *argv[])
{

    std::signal(SIGINT, _signal_handler);

    auto &logger = slick::logger::Logger::instance();
    logger.clear_sinks();
#ifdef DEBUG
    logger.add_console_sink(true, true);
#endif
    logger.add_file_sink("logs/slick-sim.log");
    logger.init(65535, 16777216);

    slick::net::set_log_handler(
        [&logger](slick::net::LogLevel level, const char *format_text, std::format_args args)
        {
            logger.log(static_cast<slick::logger::LogLevel>(level), format_text, std::move(args));
        },
        [&logger]() -> slick::net::LogLevel
        {
            return static_cast<slick::net::LogLevel>(logger.get_level());
        });

    std::string config_file = "slick-sim.json";
    if (argc > 1)
    {
        config_file = argv[1];
    }

    if (!std::filesystem::exists(config_file))
    {
        std::cout << "Missing configuration JSON file. Usange\n"
                  << "    slick-sim [config_file]\n"
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
    catch (const json::parse_error &ex)
    {
        std::cerr << "Parsing failed: \n"
                  << ex.byte;
        LOG_ERROR("Parsing failed: \n{}", ex.byte);
        return EXIT_FAILURE;
    }

    if (!config.contains("exchanges"))
    {
        std::cerr << "Missing exchanges config";
        LOG_ERROR("Missing exchanges config");
        return EXIT_FAILURE;
    }

    logger.set_level(slick::logger::to_log_level(config.value("log_level", "info")));

    auto &exch_coinfig = config["exchanges"];
    std::vector<std::unique_ptr<Exchange>> exchanges;
    for (auto it = exch_coinfig.begin(); it != exch_coinfig.end(); ++it)
    {
        if (it.key() == "coinbase")
        {
            auto enabled = it.value().value("enabled", true);
            if (enabled)
            {
                exchanges.emplace_back(std::make_unique<slick::sim::exch::CoinbaseExchange>(it.value()));
                exchanges.back()->start();
            }
        }
        else if (it.key() == "hyperliquid")
        {
            auto enabled = it.value().value("enabled", true);
            if (enabled)
            {
                exchanges.emplace_back(std::make_unique<slick::sim::exch::HyperliquidExchange>(it.value()));
                exchanges.back()->start();
            }
        }
        else
        {
            exchanges.emplace_back(std::make_unique<Exchange>(to_venue(it.key()), it.value()));
            exchanges.back()->start();
        }
    }

    // Wait for user input to stop
    LOG_INFO("Press Ctrl+C to stop the server...");
    while (run.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto &exch : exchanges)
    {
        exch->stop();
    }

    LOG_INFO("ExchangeSimulator stopped");
    return EXIT_SUCCESS;
}
