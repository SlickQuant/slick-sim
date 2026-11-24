#pragma once

#include "process.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace slick::sim::integration {

enum class CliCommand : uint8_t {
    LIST_TARGETS = 1,
    SET_LOG_LEVEL = 2,
    SHUTDOWN = 3,
};

struct CliReply {
    bool ok = false;
    std::string text;

    /// One line per endpoint, as "endpoint: answer".
    bool contains(std::string_view needle) const { return text.find(needle) != std::string::npos; }
};

/// One running `slick-sim`, configured from the checked-in template with free ports and per-test
/// overrides.
///
/// Tests always use `config/coinbase_self_contained.json` as their base instead of building a
/// config from scratch. A broken sample configuration therefore also fails the suite.
class SimHarness {
public:
    SimHarness() = default;
    ~SimHarness();

    SimHarness(const SimHarness&) = delete;
    SimHarness& operator=(const SimHarness&) = delete;

    bool start(const std::string& sim_binary,
               const std::string& config_template_path,
               const std::string& work_directory,
               const nlohmann::json& overrides,
               std::string& error);

    /// Asks the CLI for SHUTDOWN and waits for the process to exit on its own.
    bool stop(std::chrono::milliseconds timeout = std::chrono::seconds(15));

    CliReply cli(CliCommand command,
                 std::string_view target,
                 std::string_view argument = {},
                 uint8_t flag = 0,
                 std::chrono::milliseconds timeout = std::chrono::seconds(5));

    bool running();
    int exit_code() const { return process_.exit_code(); }
    uint32_t pid() const { return process_.pid(); }
    /// The simulator process, for tests that stop it and wait themselves.
    ChildProcess& process() { return process_; }
    uint16_t rest_port() const { return rest_port_; }
    uint16_t ws_port() const { return ws_port_; }
    uint16_t md_port() const { return md_port_; }
    uint16_t cli_port() const { return cli_port_; }
    const std::string& work_directory() const { return work_directory_; }
    const std::string& config_path() const { return config_path_; }

    /// `logs/slick-sim.log`, always written by the simulator.
    std::string log() const;
    /// Polls the log until `needle` appears at or after byte `from`. A line can land after the reply
    /// that caused it: the logger writes on its own thread, and each gateway logs its own reports.
    bool wait_for_log(std::string_view needle,
                      size_t from = 0,
                      std::chrono::milliseconds timeout = std::chrono::seconds(10)) const;
    /// The process's own stdout and stderr, for failures before logging starts.
    std::string stdio_text() const;

private:
    ChildProcess process_;
    std::string work_directory_;
    std::string config_path_;
    uint16_t rest_port_ = 0;
    uint16_t ws_port_ = 0;
    uint16_t md_port_ = 0;
    uint16_t cli_port_ = 0;
};

/// Deep-merges `overrides` into `base`, object by object.
void merge_json(nlohmann::json& base, const nlohmann::json& overrides);

}  // namespace slick::sim::integration
