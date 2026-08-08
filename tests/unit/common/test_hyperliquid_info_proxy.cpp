#include <gtest/gtest.h>
#include <common/hyperliquid_info_proxy.hpp>
#include <nlohmann/json.hpp>

using namespace slick::sim;
using json = nlohmann::json;
using Http = slick::net::Http;

// slick::net::Http reports connection failures (refused, timeout, DNS
// failure, ...) the same way it reports a genuine upstream 500: result_code
// 500 with result_text holding a raw exception message rather than a JSON
// body. build_proxied_response() must tell these apart without depending on
// real network timing.

TEST(HyperliquidInfoProxy, TransportFailure_ReturnsBadGateway) {
    Http::Response rsp{500, "Internal Server Error", "Timeout"};

    auto proxied = build_proxied_response(rsp);

    EXPECT_EQ(proxied.status_line, "502 Bad Gateway");
    json body;
    ASSERT_NO_THROW(body = json::parse(proxied.body));
    EXPECT_EQ(body.value("status", ""), "err");
}

TEST(HyperliquidInfoProxy, EmptyTransportError_ReturnsBadGateway) {
    Http::Response rsp{500, "Internal Server Error", ""};

    auto proxied = build_proxied_response(rsp);

    EXPECT_EQ(proxied.status_line, "502 Bad Gateway");
}

TEST(HyperliquidInfoProxy, RealUpstreamSuccess_RelaysBodyVerbatim) {
    json meta = {{"universe", json::array({json{{"name", "BTC"}, {"szDecimals", 5}}})}};
    Http::Response rsp{200, "OK", meta.dump()};

    auto proxied = build_proxied_response(rsp);

    EXPECT_EQ(proxied.status_line, "200 OK");
    EXPECT_EQ(json::parse(proxied.body), meta);
}

TEST(HyperliquidInfoProxy, RealUpstreamJsonError_RelaysVerbatim) {
    // A genuine upstream error response is still valid JSON, unlike a local
    // transport failure — it must be relayed, not swallowed into 502.
    json err = {{"error", "unknown type"}};
    Http::Response rsp{400, "Bad Request", err.dump()};

    auto proxied = build_proxied_response(rsp);

    EXPECT_EQ(proxied.status_line, "400 Bad Request");
    EXPECT_EQ(json::parse(proxied.body), err);
}

TEST(HyperliquidInfoProxy, MissingReason_DefaultsToOk) {
    Http::Response rsp{200, "", R"({"universe":[]})"};

    auto proxied = build_proxied_response(rsp);

    EXPECT_EQ(proxied.status_line, "200 OK");
}
