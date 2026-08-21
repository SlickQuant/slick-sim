#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <slick/logger.hpp>
// #include <slick/socket/tcp_server.h>
// #include <order_gateway/order_gateway.hpp>
// #include <order_gateway/message_parser.hpp>
#include <exchange/exchange.hpp>
#include <exchange/exchange_registry.hpp>
#include <common/messages.hpp>
// #include <market_data_publisher/market_data_publisher.hpp>
#ifdef SLICK_SIM_HAS_SLICK_NET
// slick::net belongs to the venue adapters that speak HTTP/WebSocket, not to the
// simulator: it arrives only because such an adapter declared it, so the log bridge
// below is compiled in only then. A core-only build has no networking to bridge,
// and neither would a venue on a different stack - a CME or ICE adapter over
// slick::socket. main.cpp still names no venue: the flag is set from the link list.
#include <slick/net/logging.hpp>
#endif
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

#ifdef SLICK_SIM_HAS_SLICK_NET
    slick::net::set_log_handler(
        [&logger](slick::net::LogLevel level, const char *format_text, std::format_args args)
        {
            logger.log(static_cast<slick::logger::LogLevel>(level), format_text, std::move(args));
        },
        [&logger]() -> slick::net::LogLevel
        {
            return static_cast<slick::net::LogLevel>(logger.get_level());
        });
#endif

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

    auto &registry = slick::sim::exch::ExchangeRegistry::instance();

    // A venue that was compiled out otherwise looks exactly like a typo in the
    // config, so say up front which adapters this binary actually carries.
    std::string built_in;
    for (const auto &key : registry.keys())
    {
        if (!built_in.empty())
        {
            built_in += ", ";
        }
        built_in += key;
    }
    LOG_INFO("Venue adapters built in: {}", built_in.empty() ? "(none)" : built_in);

    auto &exch_coinfig = config["exchanges"];
    std::vector<std::unique_ptr<Exchange>> exchanges;
    for (auto it = exch_coinfig.begin(); it != exch_coinfig.end(); ++it)
    {
        // Checked BEFORE the registry lookup, deliberately: the committed sample
        // config carries a "cme" block with "enabled": false, and a disabled venue
        // must not require an adapter to exist.
        if (!it.value().value("enabled", true))
        {
            LOG_INFO("Exchange {} is disabled, skipping", it.key());
            continue;
        }

        auto factory = registry.find(it.key());
        if (!factory)
        {
            // This used to fall through to a bare Exchange, which bound no ports and
            // processed at most one request before going quiet. Failing loudly is
            // also what turns a venue whose registration the linker dropped into an
            // obvious error rather than a mystery.
            std::cerr << "No venue adapter for exchanges.\"" << it.key() << "\"\n";
            LOG_ERROR("No venue adapter for exchanges.\"{}\". This build has: {}. "
                      "Check the spelling, or configure with -DSLICK_SIM_ENABLE_<VENUE>=ON.",
                      it.key(), built_in.empty() ? "(none)" : built_in);
            return EXIT_FAILURE;
        }

        exchanges.emplace_back(factory(it.value()));
        exchanges.back()->start();
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
