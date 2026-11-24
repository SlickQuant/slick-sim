#include "sim_harness.hpp"

#include "common/control_messages.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace slick::sim::integration {

namespace {

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalid = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalid = -1;
#endif

#if defined(_WIN32)
void ensureWinsock() {
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)ready;
}
#endif

void closeSocket(socket_t socket) {
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

socket_t connectToPort(uint16_t port, std::chrono::milliseconds timeout) {
#if defined(_WIN32)
    ensureWinsock();
#endif
    const socket_t socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kInvalid) {
        return kInvalid;
    }
#if defined(_WIN32)
    DWORD ms = static_cast<DWORD>(timeout.count());
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(socket);
        return kInvalid;
    }
    return socket;
}

bool sendAll(socket_t socket, const void* data, size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < size) {
        const auto count = ::send(socket, bytes + sent, static_cast<int>(size - sent), 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool receiveAll(socket_t socket, void* data, size_t size) {
    auto* bytes = static_cast<char*>(data);
    size_t received = 0;
    while (received < size) {
        const auto count = ::recv(socket, bytes + received, static_cast<int>(size - received), 0);
        if (count <= 0) {
            return false;
        }
        received += static_cast<size_t>(count);
    }
    return true;
}

}  // namespace

void merge_json(nlohmann::json& base, const nlohmann::json& overrides) {
    if (overrides.is_null()) {
        return;
    }
    if (!base.is_object() || !overrides.is_object()) {
        base = overrides;
        return;
    }
    for (const auto& [key, value] : overrides.items()) {
        if (base.contains(key)) {
            merge_json(base[key], value);
        } else {
            base[key] = value;
        }
    }
}

std::string SimHarness::log() const {
    return read_file((std::filesystem::path(work_directory_) / "logs" / "slick-sim.log").string());
}

bool SimHarness::wait_for_log(std::string_view needle, size_t from, std::chrono::milliseconds timeout) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (log().find(needle, from) == std::string::npos) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

std::string SimHarness::stdio_text() const {
    return "--- stdout ---\n" + read_file((std::filesystem::path(work_directory_) / "slick-sim.out").string()) +
           "\n--- stderr ---\n" +
           read_file((std::filesystem::path(work_directory_) / "slick-sim.err").string());
}

bool SimHarness::start(const std::string& sim_binary,
                       const std::string& config_template_path,
                       const std::string& work_directory,
                       const nlohmann::json& overrides,
                       std::string& error) {
    work_directory_ = work_directory;
    std::error_code ec;
    std::filesystem::remove_all(work_directory_, ec);
    std::filesystem::create_directories(std::filesystem::path(work_directory_) / "logs", ec);
    if (ec) {
        error = "cannot create the simulator's working directory: " + ec.message();
        return false;
    }

    const std::string template_text = read_file(config_template_path);
    if (template_text.empty()) {
        error = "cannot read the config template at " + config_template_path;
        return false;
    }
    nlohmann::json config;
    try {
        config = nlohmann::json::parse(template_text);
    } catch (const std::exception& ex) {
        error = std::string("the config template is not valid JSON: ") + ex.what();
        return false;
    }

    do {
        rest_port_ = free_tcp_port();
        ws_port_ = free_tcp_port();
        md_port_ = free_tcp_port();
        cli_port_ = free_tcp_port();
    } while (rest_port_ == 0 || ws_port_ == 0 || md_port_ == 0 || cli_port_ == 0 ||
             rest_port_ == ws_port_ || rest_port_ == md_port_ || rest_port_ == cli_port_ ||
             ws_port_ == md_port_ || ws_port_ == cli_port_ || md_port_ == cli_port_);

    config["exchanges"]["coinbase"]["order_gateway"]["rest"]["port"] = rest_port_;
    config["exchanges"]["coinbase"]["order_gateway"]["ws"]["port"] = ws_port_;
    config["exchanges"]["coinbase"]["md_publisher"]["port"] = md_port_;
    config["cli"]["port"] = cli_port_;
    merge_json(config, overrides);

    config_path_ = (std::filesystem::path(work_directory_) / "config.json").string();
    {
        std::ofstream out(config_path_, std::ios::binary | std::ios::trunc);
        out << config.dump(4) << '\n';
    }

    if (!process_.start(sim_binary, {config_path_}, work_directory_,
                        (std::filesystem::path(work_directory_) / "slick-sim.out").string(),
                        (std::filesystem::path(work_directory_) / "slick-sim.err").string(), error)) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (const uint16_t port : {rest_port_, ws_port_, md_port_, cli_port_}) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0 || !wait_for_port(port, left)) {
            error = "slick-sim did not listen on port " + std::to_string(port) + "\n" + stdio_text() +
                    "\n--- log ---\n" + log();
            process_.terminate();
            return false;
        }
    }
    return true;
}

bool SimHarness::stop(std::chrono::milliseconds timeout) {
    if (!process_.running()) {
        return process_.exited() && process_.exit_code() == 0;
    }
    cli(CliCommand::SHUTDOWN, "sim");
    const auto code = process_.wait_for_exit(timeout);
    if (!code) {
        process_.terminate();
        return false;
    }
    return *code == 0;
}

CliReply SimHarness::cli(CliCommand command,
                         std::string_view target,
                         std::string_view argument,
                         uint8_t flag,
                         std::chrono::milliseconds timeout) {
    CliReply reply;
    const socket_t socket = connectToPort(cli_port_, timeout);
    if (socket == kInvalid) {
        reply.text = "cannot connect to the CLI port";
        return reply;
    }

    static std::atomic<uint32_t> next_request{0};
    slick::sim::CliRequestMessage request{};
    request.header.length = static_cast<uint16_t>(sizeof(request));
    request.header.type = slick::sim::ControlMessageType::CLI_REQUEST;
    request.request_id = ++next_request;
    request.command = static_cast<slick::sim::CliCommandType>(static_cast<uint8_t>(command));
    request.flag = flag;
    // copy() rather than memcpy: an empty view's data() may be null, which memcpy forbids even
    // for a zero length (UBSan halts on it).
    target.copy(request.target, sizeof(request.target));
    argument.copy(request.arg, sizeof(request.arg));

    if (!sendAll(socket, &request, sizeof(request))) {
        reply.text = "cannot send the CLI request";
        closeSocket(socket);
        return reply;
    }

    slick::sim::CliReplyMessage header{};
    bool ok = receiveAll(socket, &header, sizeof(header));
    if (ok && header.header.length > sizeof(header)) {
        std::string text(header.header.length - sizeof(header), '\0');
        ok = receiveAll(socket, text.data(), text.size());
        reply.text = std::move(text);
    }
    closeSocket(socket);
    if (!ok) {
        reply.text = "the CLI port closed the connection";
        return reply;
    }
    reply.ok = header.status == slick::sim::CliStatus::OK;
    return reply;
}

bool SimHarness::running() {
    return process_.running();
}

SimHarness::~SimHarness() {
    process_.terminate();
}

}  // namespace slick::sim::integration
