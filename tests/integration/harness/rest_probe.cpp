#include "rest_probe.hpp"

#include <slick/net/http.hpp>

#include <chrono>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace slick::sim::integration {

namespace {

HttpResult run(std::string_view url, HttpHeaders headers, std::string_view body, bool post) {
    auto response = post ? slick::net::Http::post(url, body, std::move(headers))
                         : slick::net::Http::get(url, std::move(headers));
    return HttpResult{response.result_code, std::move(response.result_text)};
}

}  // namespace

HttpResult httpGet(std::string_view url, HttpHeaders headers) {
    return run(url, std::move(headers), {}, false);
}

HttpResult httpPost(std::string_view url, std::string_view body, HttpHeaders headers) {
    return run(url, std::move(headers), body, true);
}

bool httpPostAndHangUp(std::string_view host_port, std::string_view path,
                       std::string_view body, const HttpHeaders &headers) {
    const auto colon = host_port.rfind(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::string host(host_port.substr(0, colon));
    const std::string port_text(host_port.substr(colon + 1));
    const auto port = static_cast<uint16_t>(std::stoi(port_text));

#if defined(_WIN32)
    // Winsock is process-wide; the first caller starts it.
    static const bool winsock_ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!winsock_ready) {
        return false;
    }
    const SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        return false;
    }
    const auto close_socket = [fd] { ::closesocket(fd); };
#else
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    const auto close_socket = [fd] { ::close(fd); };
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        close_socket();
        return false;
    }

    std::string request = "POST " + std::string(path) + " HTTP/1.1\r\n";
    request += "Host: " + host + ":" + port_text + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "Connection: close\r\n";
    for (const auto &[name, value] : headers) {
        request += name + ": " + value + "\r\n";
    }
    request += "\r\n";
    request.append(body);

    size_t sent = 0;
    while (sent < request.size()) {
        const auto written = ::send(fd, request.data() + sent,
                                    static_cast<int>(request.size() - sent), 0);
        if (written <= 0) {
            close_socket();
            return false;
        }
        sent += static_cast<size_t>(written);
    }

    // The body is on its way; hanging up now leaves the simulator to process the
    // request and then discover that nobody is listening for the answer.
    close_socket();
    return true;
}

std::string base64Url(std::string_view data) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded;
    encoded.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        const uint32_t chunk = (static_cast<uint8_t>(data[i]) << 16) |
                               (static_cast<uint8_t>(data[i + 1]) << 8) |
                               static_cast<uint8_t>(data[i + 2]);
        encoded += alphabet[(chunk >> 18) & 0x3F];
        encoded += alphabet[(chunk >> 12) & 0x3F];
        encoded += alphabet[(chunk >> 6) & 0x3F];
        encoded += alphabet[chunk & 0x3F];
        i += 3;
    }
    const size_t remaining = data.size() - i;
    if (remaining == 1) {
        const uint32_t chunk = static_cast<uint8_t>(data[i]) << 16;
        encoded += alphabet[(chunk >> 18) & 0x3F];
        encoded += alphabet[(chunk >> 12) & 0x3F];
    } else if (remaining == 2) {
        const uint32_t chunk = (static_cast<uint8_t>(data[i]) << 16) |
                               (static_cast<uint8_t>(data[i + 1]) << 8);
        encoded += alphabet[(chunk >> 18) & 0x3F];
        encoded += alphabet[(chunk >> 12) & 0x3F];
        encoded += alphabet[(chunk >> 6) & 0x3F];
    }
    return encoded;
}

std::string bearerToken(std::string_view user) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    nlohmann::json header{{"alg", "HS256"}, {"typ", "JWT"}};
    nlohmann::json payload{{"iss", "cdp"},
                           {"sub", "organizations/integration-test/actors/" + std::string(user)},
                           {"nbf", now},
                           {"exp", now + 3600}};
    // The signature segment only has to exist: the gateway calls jwt::decode,
    // which parses without verifying.
    return base64Url(header.dump()) + "." + base64Url(payload.dump()) + ".c2ln";
}

}  // namespace slick::sim::integration
