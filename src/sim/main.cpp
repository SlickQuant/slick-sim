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
#include <cli/cli_endpoint.hpp>
#ifdef SLICK_SIM_HAS_CLI
#include <cli/cli_server.hpp>
#endif
// #include <market_data_publisher/market_data_publisher.hpp>
#ifdef SLICK_SIM_HAS_SLICK_NET
// slick::net belongs to the venue adapters that speak HTTP/WebSocket, not to the
// simulator: it arrives only because such an adapter declared it, so the log bridge
// below is compiled in only then. A core-only build has no networking to bridge,
// and neither would a venue on a different stack - a CME or ICE adapter over
// slick::socket. main.cpp still names no venue: the flag is set from the link list.
#include <slick/net/logging.hpp>
#endif
#include <exception>
#include <format>
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

    /// The `sim` target's commands, on the main thread: what belongs to the process
    /// rather than to a venue.
    bool serve_sim_command(const cli::Command &command, std::string &out)
    {
        auto &logger = slick::logger::Logger::instance();
        switch (command.command)
        {
        case CliCommandType::SET_LOG_LEVEL:
            if (!command.arg_view().empty())
            {
                try
                {
                    logger.set_level(slick::logger::to_log_level(command.arg_view()));
                }
                catch (const std::invalid_argument &)
                {
                    out = std::format("unknown log level '{}'", command.arg_view());
                    return false;
                }
            }
            out = std::format("log level {}", slick::logger::to_string(logger.get_level()));
            return true;
        case CliCommandType::SHUTDOWN:
            run.store(false, std::memory_order_release);
            out = "stopping";
            return true;
        default:
            out = std::format("{} is not served here", to_string(command.command));
            return false;
        }
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
    // The CLI endpoints of each venue config key's exchanges, which that key addresses.
    std::vector<std::pair<std::string, std::vector<cli::CliEndpoint *>>> cli_groups;
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

        // A venue split into instances (an exchange's market segments) yields several.
        // A venue rejects a config it cannot run by throwing - say why, rather
        // than terminating on an uncaught exception.
        std::vector<std::unique_ptr<Exchange>> created;
        try
        {
            created = factory(it.value());
        }
        catch (const std::exception &ex)
        {
            std::cerr << "exchanges.\"" << it.key() << "\": " << ex.what() << "\n";
            LOG_ERROR("exchanges.\"{}\": {}", it.key(), ex.what());
            return EXIT_FAILURE;
        }
        auto &group = cli_groups.emplace_back(it.key(), std::vector<cli::CliEndpoint *>{}).second;
        for (auto &exchange : created)
        {
            exchanges.emplace_back(std::move(exchange));
            exchanges.back()->start();
            auto endpoints = exchanges.back()->cliEndpoints();
            group.insert(group.end(), endpoints.begin(), endpoints.end());
        }
    }

    // Declared after `exchanges`, so destroyed first: the server reads the endpoints
    // the exchanges' gateways own.
    cli::CliEndpoint sim_cli("sim");
    bool cli_running = false;
#ifdef SLICK_SIM_HAS_CLI
    std::unique_ptr<cli::CliServer> cli_server;
#endif
    if (config.contains("cli") && config["cli"].value("enabled", true))
    {
#ifdef SLICK_SIM_HAS_CLI
        const auto &cli_config = config["cli"];
        slick::socket::TCPServerConfig tcp;
        tcp.port = cli_config.value("port", cli::CliServer::DEFAULT_PORT);
        tcp.max_connections = cli_config.value("max_connections", 4);
        tcp.idle_timeout = std::chrono::seconds(
            cli_config.value("idle_timeout", cli::CliServer::DEFAULT_IDLE_TIMEOUT.count()));
        cli_server = std::make_unique<cli::CliServer>(tcp);
        cli_server->add_endpoints("sim", {&sim_cli});
        for (const auto &[key, endpoints] : cli_groups)
        {
            cli_server->add_endpoints(key, endpoints);
        }
        if (!cli_server->start())
        {
            std::cerr << "CLI failed to start on port " << tcp.port << "\n";
            return EXIT_FAILURE;
        }
        cli_running = true;
#else
        LOG_WARN("\"cli\" is configured, but this build has no CLI: it needs SLICK_SIM_ENABLE_TCP_GATEWAY");
#endif
    }

    // Wait for user input to stop
    LOG_INFO("Press Ctrl+C to stop the server...");
    while (run.load(std::memory_order_relaxed))
    {
        sim_cli.serve(serve_sim_command);
        std::this_thread::sleep_for(cli_running ? std::chrono::milliseconds(10) : std::chrono::milliseconds(1000));
    }

#ifdef SLICK_SIM_HAS_CLI
    if (cli_server)
    {
        // Lets the answer to a SHUTDOWN go out, then stops the server before the
        // exchanges, whose gateways own its endpoints.
        cli_server->flush(std::chrono::milliseconds(500));
        cli_server->stop();
    }
#endif

    for (auto &exch : exchanges)
    {
        exch->stop();
    }

    LOG_INFO("ExchangeSimulator stopped");
    return EXIT_SUCCESS;
}
