#include "test_environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace slick::sim::integration {

namespace {

CoinbaseIntegrationEnvironment* g_environment = nullptr;

std::string argument(int argc, char** argv, std::string_view flag) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (flag == argv[i]) {
            return argv[i + 1];
        }
    }
    return {};
}

std::string environmentVariable(const char* name) {
#if defined(_WIN32)
    // _dupenv_s: plain getenv trips MSVC's C4996.
    char* value = nullptr;
    size_t size = 0;
    std::string result;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        result = value;
    }
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

}  // namespace

Paths parsePaths(int argc, char** argv) {
    Paths paths;
    paths.sim_binary = argument(argc, argv, "--sim");
    if (paths.sim_binary.empty()) {
        paths.sim_binary = environmentVariable("SLICK_SIM_BINARY");
    }
    paths.client_binary = argument(argc, argv, "--client");
    if (paths.client_binary.empty()) {
        paths.client_binary = environmentVariable("SLICK_SIM_CLIENT_BINARY");
    }
    paths.config_template = argument(argc, argv, "--config");
    if (paths.config_template.empty()) {
        paths.config_template = environmentVariable("SLICK_SIM_CONFIG_TEMPLATE");
    }
    paths.work_directory = argument(argc, argv, "--workdir");
    if (paths.work_directory.empty()) {
        paths.work_directory = environmentVariable("SLICK_SIM_INTEGRATION_WORKDIR");
    }
    if (paths.work_directory.empty()) {
        paths.work_directory = (std::filesystem::current_path() / ".integration-coinbase").string();
    }
    return paths;
}

void setEnvironment(CoinbaseIntegrationEnvironment* environment) {
    g_environment = environment;
}

CoinbaseIntegrationEnvironment& env() {
    return *g_environment;
}

ClientDriver& CoinbaseIntegrationEnvironment::client(const std::string& user) {
    for (auto& client : clients_) {
        if (client.user() == user) {
            return client;
        }
    }
    throw std::runtime_error("no client named " + user);
}

void CoinbaseIntegrationEnvironment::SetUp() {
    if (paths_.sim_binary.empty() || paths_.client_binary.empty() || paths_.config_template.empty()) {
        error_ = "missing --sim/--client/--config (or SLICK_SIM_BINARY, SLICK_SIM_CLIENT_BINARY, "
                 "SLICK_SIM_CONFIG_TEMPLATE); run through ctest or pass them explicitly";
        return;
    }

    std::string error;
    if (!sim_.start(paths_.sim_binary, paths_.config_template, paths_.work_directory, {}, error)) {
        error_ = "the simulator did not start:\n" + error;
        return;
    }

    const std::array<const char*, 3> users{"alice", "bob", "carol"};
    for (size_t i = 0; i < users.size(); ++i) {
        if (!clients_[i].start(paths_.client_binary, users[i], sim_.rest_port(), sim_.ws_port(), sim_.md_port(),
                               paths_.work_directory, error)) {
            error_ = std::string("client ") + users[i] + " did not start:\n" + error;
            return;
        }
    }
    for (auto& client : clients_) {
        if (client.wait_for([](const nlohmann::json& event) { return event.value("event", "") == "ready"; },
                            std::chrono::seconds(20)) == nullptr) {
            error_ = "client " + client.user() + " never reported ready\nthe simulator log:\n" + sim_.log() +
                     "\nthe client stderr:\n" + client.stderr_text();
            return;
        }
    }
    ok_ = true;
}

void CoinbaseIntegrationEnvironment::TearDown() {
    if (!ok_) {
        sim_.stop();
        return;
    }
    for (auto& client : clients_) {
        client.quit();
    }
    std::string error;
    if (!sim_.stop()) {
        // Not a test failure by itself: the dedicated shutdown test owns that
        // assertion. Print enough to diagnose a port left behind.
        std::fprintf(stderr, "the simulator did not stop cleanly (exit %d)\n%s\n%s\n", sim_.exit_code(),
                     sim_.stdio_text().c_str(), sim_.log().c_str());
    }
}

}  // namespace slick::sim::integration
