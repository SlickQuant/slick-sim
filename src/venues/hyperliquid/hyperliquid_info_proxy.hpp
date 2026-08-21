#pragma once

#include <slick/logger.hpp>
#include <slick/net/http.hpp>
#include <uwebsockets/App.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace slick::sim {

struct ProxiedHttpResponse {
    std::string status_line;
    std::string body;
};

// Decides what to relay to the client given the raw response from the
// upstream Hyperliquid REST API. Pure/synchronous (no I/O) so it can be unit
// tested directly against synthetic slick::net::Http::Response values,
// without depending on real network timing.
//
// slick::net::Http reports both a genuine upstream error and a local
// transport failure (connection refused, timeout, DNS failure, ...) via
// result_code=500 — in the transport-failure case, result_text holds the raw
// exception message, not a JSON body. Distinguish the two by whether the
// body actually parses as JSON.
inline ProxiedHttpResponse build_proxied_response(const slick::net::Http::Response& rsp) {
    bool upstream_reached = false;
    try {
        [[maybe_unused]] auto result = nlohmann::json::parse(rsp.result_text);
        upstream_reached = true;
    } catch (...) {}

    if (!upstream_reached) {
        return {"502 Bad Gateway", R"({"status":"err","response":"failed to reach upstream Hyperliquid API"})"};
    }

    std::string status_line = std::to_string(rsp.result_code) + " " +
                               (rsp.reason.empty() ? "OK" : rsp.reason);
    return {std::move(status_line), rsp.result_text};
}

// Proxies a client's POST /info request to the real Hyperliquid REST API and
// relays the response back verbatim. Shared by HyperliquidPublisher (md
// publisher, port 5001) and HyperliquidRestOrderGateway (order gateway, port
// 4003) so whichever port a hyperliquid-cpp `Info` client points its
// base_url at, `load_meta()`/`canonical_coin()` (and any other /info query)
// get real production data instead of a local stub.
inline void proxy_hyperliquid_info_request(
    uWS::HttpResponse<false>* res,
    const std::string& upstream_base_url)
{
    auto body_buffer = std::make_shared<std::string>();

    res->onAborted([]() {
        LOG_WARN("HyperliquidInfoProxy: POST /info aborted");
    });

    res->onData([res, body_buffer, upstream_base_url](std::string_view chunk, bool is_last) {
        body_buffer->append(chunk);
        if (!is_last) return;

        auto rsp = slick::net::Http::post(
            upstream_base_url + "/info", *body_buffer,
            {{"Content-Type", "application/json"}});

        auto proxied = build_proxied_response(rsp);
        if (proxied.status_line == "502 Bad Gateway") {
            LOG_WARN("HyperliquidInfoProxy: failed to reach upstream {}/info: {}",
                     upstream_base_url, rsp.result_text);
        }

        res->writeStatus(proxied.status_line)
           ->writeHeader("Content-Type", "application/json")
           ->end(proxied.body);
    });
}

}   // end namespace slick::sim
