#include "client_driver.hpp"

#include <filesystem>
#include <sstream>

namespace slick::sim::integration {

bool ClientDriver::start(const std::string& client_binary,
                         const std::string& user,
                         uint16_t rest_port,
                         uint16_t ws_port,
                         uint16_t md_port,
                         const std::string& work_directory,
                         std::string& error) {
    user_ = user;
    const std::filesystem::path directory(work_directory);
    // Redirect the client's stdout event stream to a file. Keep SDK logs in stderr.
    events_path_ = (directory / (user + ".out")).string();
    stderr_path_ = (directory / (user + ".err")).string();

    return process_.start(client_binary,
                          {"--user", user,
                           "--rest-port", std::to_string(rest_port),
                           "--ws-port", std::to_string(ws_port),
                           "--md-port", std::to_string(md_port),
                           "--product", "BTC-USD"},
                          work_directory,
                          (directory / (user + ".out")).string(),
                          stderr_path_,
                          error);
}

bool ClientDriver::send(const nlohmann::json& command) {
    return process_.write_line(command.dump());
}

void ClientDriver::poll() {
    const std::string text = read_file(events_path_);
    size_t processed = 0;
    size_t line_start = 0;
    while (line_start < text.size()) {
        const auto newline = text.find('\n', line_start);
        if (newline == std::string::npos) {
            break;   // a line still being written
        }
        std::string line = text.substr(line_start, newline - line_start);
        line_start = newline + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        ++processed;
        if (processed <= consumed_lines_) {
            continue;
        }
        if (line.empty()) {
            continue;
        }
        try {
            events_.push_back(nlohmann::json::parse(line));
        } catch (const std::exception&) {
            // A line the client could not finish writing; skip it rather than
            // failing the whole stream.
        }
    }
    consumed_lines_ = processed;
}

bool ClientDriver::quit(std::chrono::milliseconds timeout) {
    if (!process_.running()) {
        return process_.exited();
    }
    send({{"cmd", "quit"}});
    if (process_.wait_for_exit(timeout)) {
        return true;
    }
    process_.terminate();
    return false;
}

bool ClientDriver::running() {
    return process_.running();
}

std::string ClientDriver::stderr_text() const {
    return read_file(stderr_path_);
}

}  // namespace slick::sim::integration
