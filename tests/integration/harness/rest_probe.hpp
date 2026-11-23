#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace slick::sim::integration {

struct HttpResult {
    uint32_t status = 0;
    std::string body;

    bool ok() const { return status >= 200 && status < 300; }
    bool contains(std::string_view needle) const { return body.find(needle) != std::string::npos; }
};

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

HttpResult httpGet(std::string_view url, HttpHeaders headers = {});
HttpResult httpPost(std::string_view url, std::string_view body, HttpHeaders headers = {});

/// Sends a complete HTTP/1.1 POST to `host_port` ("127.0.0.1:1234"), then closes the socket without
/// reading the reply. The simulator processes the request after the client has gone. This lets a
/// test abandon an HTTP completion from outside the server; the SDK always waits for its response.
bool httpPostAndHangUp(std::string_view host_port, std::string_view path,
                       std::string_view body, const HttpHeaders &headers = {});

/// A syntactically valid JWT carrying `sub = organizations/integration-test/actors/<user>`.
/// The simulator decodes it and derives the user id from the last `/`; it never
/// verifies the signature, which docs/integration-coinbase.md documents.
std::string bearerToken(std::string_view user);

/// Unpadded base64url, RFC 4648 section 5.
std::string base64Url(std::string_view data);

}  // namespace slick::sim::integration
