#pragma once

#include <cli/cli_endpoint.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace slick::sim::test {

struct CliAnswer {
    CliStatus status;
    std::string text;
};

/// A command to `cli::Command`, ticket and all.
inline cli::Command cli_command(uint64_t ticket, CliCommandType type, uint8_t flag = 0, std::string_view arg = {}) {
    cli::Command command{ticket, type, flag, {}};
    std::memcpy(command.arg, arg.data(), std::min(arg.size(), sizeof(command.arg)));
    return command;
}

/// Posts a command to `endpoint` as the CLI server would and waits for its answer. The
/// endpoint's owner (a gateway's thread, say) must be serving it meanwhile. None when
/// the endpoint is busy.
inline std::optional<CliAnswer> cli_call(cli::CliEndpoint &endpoint, CliCommandType type, uint8_t flag = 0,
                                         std::string_view arg = {},
                                         std::chrono::milliseconds limit = std::chrono::seconds(5)) {
    static uint64_t next_ticket = 1;
    const auto command = cli_command(next_ticket++, type, flag, arg);
    if (!endpoint.post(command)) {
        return std::nullopt;
    }
    std::optional<CliAnswer> answer;
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!answer && std::chrono::steady_clock::now() < deadline) {
        endpoint.take_replies([&](uint64_t ticket, CliStatus status, std::string_view text) {
            if (ticket == command.ticket) {
                answer = CliAnswer{status, std::string(text)};
            }
        });
        if (!answer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return answer;
}

}   // namespace slick::sim::test
