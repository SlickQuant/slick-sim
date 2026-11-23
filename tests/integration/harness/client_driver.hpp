#pragma once

#include "process.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace slick::sim::integration {

/// One `slick_sim_coinbase_client` child receives JSON Lines commands on stdin. The driver tails
/// the child's event file.
///
/// The file keeps the complete event history available after a failure. The client flushes each
/// line, so tailing reads every complete event.
class ClientDriver {
public:
    ClientDriver() = default;
    ~ClientDriver() = default;

    ClientDriver(const ClientDriver&) = delete;
    ClientDriver& operator=(const ClientDriver&) = delete;

    bool start(const std::string& client_binary,
               const std::string& user,
               uint16_t rest_port,
               uint16_t ws_port,
               uint16_t md_port,
               const std::string& work_directory,
               std::string& error);

    bool send(const nlohmann::json& command);

    /// Reads whatever complete lines have been appended since the last call.
    void poll();

    /// Polls until an event satisfies `predicate`, or `timeout` elapses. The
    /// returned pointer stays valid until the next poll(); nullptr means timeout.
    template <typename Predicate>
    const nlohmann::json* wait_for(Predicate predicate,
                                   std::chrono::milliseconds timeout,
                                   std::vector<nlohmann::json>* seen = nullptr) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            poll();
            if (seen != nullptr) {
                *seen = events_;
            }
            for (const auto& event : events_) {
                if (predicate(event)) {
                    return &event;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return nullptr;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    const std::vector<nlohmann::json>& events() const { return events_; }

    /// Drains pending lines before clearing parsed events. This prevents an event already written
    /// by the client from appearing after the clear and being mistaken for a later event.
    void clear_events() {
        poll();
        events_.clear();
    }

    /// Sends quit and waits for the client to flush its streams and exit.
    bool quit(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    bool running();
    std::string stderr_text() const;
    const std::string& user() const { return user_; }
    ChildProcess& process() { return process_; }

private:
    ChildProcess process_;
    std::string user_;
    std::string events_path_;
    std::string stderr_path_;
    size_t consumed_lines_ = 0;
    std::vector<nlohmann::json> events_;
};

}  // namespace slick::sim::integration
