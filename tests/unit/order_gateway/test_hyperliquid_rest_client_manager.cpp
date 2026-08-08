#include <gtest/gtest.h>
#include <order_gateway/hyperliquid_rest_order_gateway.hpp>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <slick/queue.h>
#include <slick/net/http.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <format>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::order_gateway;
using json = nlohmann::json;
using Http = slick::net::Http;

static constexpr uint16_t kTestPort      = 19003;
static const char* const  kTestBaseUrl   = "http://127.0.0.1:1"; // fast "connection refused"
static const char* const  kServerUrl     = "http://127.0.0.1:19003";
static const char* const  kTestWallet    = "0xTestWallet00000000000000000000000001";

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class HyperliquidRestTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use local (non-shared-memory) queues: named shared memory segments persist
        // across process runs on Windows, leaking stale cursors/data between test runs.
        request_queue_  = std::make_unique<slick::queue<Request>>(4096, "hl_rest_req");
        response_queue_ = std::make_unique<slick::queue<OrderResponse>>(4096, "hl_rest_resp");
        req_cursor_     = 0;
        running_        = true;

        json cfg = {
            {"port",           kTestPort},
            {"base_url",       kTestBaseUrl},  // fails fast, no asset map loaded
            {"default_wallet", kTestWallet}
        };
        gateway_ = std::make_unique<HyperliquidRestOrderGateway>(
            cfg, *request_queue_, *response_queue_);

        gateway_->start();
        // Wait for uWS to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(80));

        startAutoResponder();
    }

    void TearDown() override {
        running_ = false;
        if (auto_responder_.joinable()) {
            auto_responder_.join();
        }
        gateway_->stop();
        request_queue_->reset();
        response_queue_->reset();
    }

    // Post to /exchange and return parsed response JSON.
    // Returns {} on network error.
    json post_exchange(const json& body) {
        auto rsp = Http::post(std::string(kServerUrl) + "/exchange", body.dump());
        if (!rsp.is_ok()) return {};
        try { return json::parse(rsp.result_text); }
        catch (...) { return {}; }
    }

    // Build a minimal order action body.
    // coin_or_idx: pass as string (asset map is empty, so string fallback is used)
    json makeOrderBody(const std::string& coin, bool is_buy, double price, double size,
                       const std::string& cloid = "", const std::string& tif = "Gtc") {
        json order = {
            {"a", coin},
            {"b", is_buy},
            {"p", std::format("{:.4f}", price)},
            {"s", std::format("{:.4f}", size)},
            {"t", {{"limit", {{"tif", tif}}}}}
        };
        if (!cloid.empty()) order["c"] = cloid;
        return {{"action", {{"type", "order"}, {"orders", json::array({order})}}}};
    }

    // Inject an OrderResponse for the given user_id+cloid into response_queue_.
    void injectResponse(const std::string& user_id, const std::string& cloid,
                        OrderStatus status, const std::string& oid_str) {
        auto idx  = response_queue_->reserve();
        auto* rsp = (*response_queue_)[idx];
        std::memset(rsp, 0, sizeof(*rsp));
        std::memcpy(rsp->user_id, user_id.c_str(),
                    std::min(sizeof(rsp->user_id) - 1, user_id.size()));
        std::memcpy(rsp->client_order_id, cloid.c_str(),
                    std::min(sizeof(rsp->client_order_id) - 1, cloid.size()));
        std::memcpy(rsp->order_id, oid_str.c_str(),
                    std::min(sizeof(rsp->order_id) - 1, oid_str.size()));
        rsp->order_status = status;
        rsp->exec_type    = (status == OrderStatus::REJECTED) ? ExecType::REJECTED : ExecType::NEW;
        response_queue_->publish(idx);
    }

    // Background thread: watches request_queue_, injects OrderResponse for NEW_ORDER_SINGLE.
    void startAutoResponder() {
        auto_responder_ = std::thread([this]() {
            static std::atomic<int> oid_counter{1000};
            while (running_) {
                auto [req, sz] = request_queue_->read(req_cursor_);
                if (!req) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    continue;
                }
                if (req->msg_type == MessageType::NEW_ORDER_SINGLE) {
                    // Brief delay so REST manager captures response_read_index first
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    std::string uid(req->add_order.user_id,
                                    strnlen(req->add_order.user_id, sizeof(req->add_order.user_id)));
                    std::string cloid(req->add_order.client_order_id,
                                      strnlen(req->add_order.client_order_id, sizeof(req->add_order.client_order_id)));
                    injectResponse(uid, cloid, OrderStatus::NEW,
                                   std::to_string(oid_counter.fetch_add(1)));
                }
                // For cancel/modify: no response injection (they return immediately)
            }
        });
    }

    std::unique_ptr<slick::queue<Request>>       request_queue_;
    std::unique_ptr<slick::queue<OrderResponse>> response_queue_;
    std::unique_ptr<HyperliquidRestOrderGateway> gateway_;
    std::thread  auto_responder_;
    uint64_t     req_cursor_ = 0;
    std::atomic<bool> running_{false};
};

// ===========================================================================
// Group A — Order placement
// ===========================================================================

TEST_F(HyperliquidRestTest, PlaceOrder_BuyLimit_Gtc_ReturnsResting) {
    auto body = makeOrderBody("ETH", true, 1800.0, 0.1);
    auto rsp  = post_exchange(body);

    ASSERT_EQ(rsp.value("status", ""), "ok");
    auto& statuses = rsp["response"]["data"]["statuses"];
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_TRUE(statuses[0].contains("resting"));
}

TEST_F(HyperliquidRestTest, PlaceOrder_SellLimit_Ioc_ReturnsResting) {
    auto body = makeOrderBody("BTC", false, 50000.0, 0.01, "", "Ioc");
    auto rsp  = post_exchange(body);

    ASSERT_EQ(rsp.value("status", ""), "ok");
    auto& statuses = rsp["response"]["data"]["statuses"];
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_TRUE(statuses[0].contains("resting"));
}

TEST_F(HyperliquidRestTest, PlaceOrder_WithCloid_CloidPreserved) {
    const std::string cloid = "my-test-cloid-001";
    auto body = makeOrderBody("SOL", true, 150.0, 1.0, cloid);
    auto rsp  = post_exchange(body);

    ASSERT_EQ(rsp.value("status", ""), "ok");
    EXPECT_TRUE(rsp["response"]["data"]["statuses"][0].contains("resting"));

    // Verify cloid was recorded in the request queue
    uint64_t cursor = 0;
    auto [req, sz]  = request_queue_->read(cursor);
    ASSERT_NE(req, nullptr);
    EXPECT_STREQ(req->add_order.client_order_id, cloid.c_str());
}

TEST_F(HyperliquidRestTest, PlaceOrder_RequestEnqueued_WithCorrectFields) {
    auto body = makeOrderBody("ETH", false, 2000.0, 0.5);
    post_exchange(body);

    uint64_t cursor = 0;
    auto [req, sz]  = request_queue_->read(cursor);
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->msg_type, MessageType::NEW_ORDER_SINGLE);
    EXPECT_STREQ(req->symbol, "ETH");
    EXPECT_EQ(req->add_order.side, Side::SELL);
    EXPECT_EQ(req->add_order.price, to_price_t(2000.0));
    EXPECT_EQ(req->add_order.qty, to_qty_t(0.5));
}

TEST_F(HyperliquidRestTest, PlaceOrder_MissingActionField_Returns400) {
    json bad_body = {{"not_action", "value"}};
    auto rsp_raw = Http::post(std::string(kServerUrl) + "/exchange", bad_body.dump());
    EXPECT_FALSE(rsp_raw.is_ok());
}

TEST_F(HyperliquidRestTest, PlaceOrder_RejectedResponse_ReturnsError) {
    // Use a dedicated wallet so we can inject a REJECTED response
    const std::string cloid = "reject-test-cloid";
    json order = {
        {"a", "ETH"}, {"b", true},
        {"p", "1800.0"}, {"s", "0.1"},
        {"t", {{"limit", {{"tif", "Gtc"}}}}},
        {"c", cloid}
    };
    json body = {{"action", {{"type", "order"}, {"orders", json::array({order})}}}};

    // Pre-register rejection: we need to intercept the auto-responder.
    // Override: stop the auto-responder briefly, inject a REJECTED response manually.
    // Simpler: stop auto-responder, post order, then inject REJECTED response ourselves.
    running_ = false;
    if (auto_responder_.joinable()) auto_responder_.join();

    // Post in background (will block waiting for response)
    json result;
    std::thread post_thread([&]() {
        result = post_exchange(body);
    });

    // Wait for order to be published to request_queue_
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Inject REJECTED response
    uint64_t cursor = 0;
    auto [req, sz] = request_queue_->read(cursor);
    if (req && req->msg_type == MessageType::NEW_ORDER_SINGLE) {
        std::string uid(req->add_order.user_id,
                        strnlen(req->add_order.user_id, sizeof(req->add_order.user_id)));
        injectResponse(uid, cloid, OrderStatus::REJECTED, "");
    }

    post_thread.join();
    running_ = true;
    startAutoResponder();

    ASSERT_EQ(result.value("status", ""), "ok");
    auto& statuses = result["response"]["data"]["statuses"];
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_TRUE(statuses[0].contains("error"));
}

TEST_F(HyperliquidRestTest, PlaceOrder_BatchTwoOrders_TwoStatuses) {
    json orders = json::array({
        {{"a","ETH"},{"b",true},{"p","1800.0"},{"s","0.1"},{"t",{{"limit",{{"tif","Gtc"}}}}}},
        {{"a","BTC"},{"b",false},{"p","50000.0"},{"s","0.01"},{"t",{{"limit",{{"tif","Gtc"}}}}}}
    });
    json body = {{"action", {{"type", "order"}, {"orders", orders}}}};

    auto rsp = post_exchange(body);

    ASSERT_EQ(rsp.value("status", ""), "ok");
    auto& statuses = rsp["response"]["data"]["statuses"];
    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_TRUE(statuses[0].contains("resting"));
    EXPECT_TRUE(statuses[1].contains("resting"));
}

// ===========================================================================
// Group B — Cancel actions
// ===========================================================================

TEST_F(HyperliquidRestTest, CancelByOid_RequestEnqueued) {
    json body = {{"action", {
        {"type", "cancel"},
        {"cancels", json::array({{{"a", "ETH"}, {"o", 42}}})}
    }}};
    auto rsp = post_exchange(body);

    ASSERT_EQ(rsp.value("status", ""), "ok");
    EXPECT_EQ(rsp["response"]["data"]["statuses"][0].value("success", -1), 42);

    uint64_t cursor = 0;
    auto [req, sz]  = request_queue_->read(cursor);
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->msg_type, MessageType::ORDER_CANCEL_REQUEST);
    EXPECT_STREQ(req->symbol, "ETH");
}

TEST_F(HyperliquidRestTest, CancelByCloid_RequestEnqueued) {
    const std::string cloid = "cancel-by-cloid-123";
    json body = {{"action", {
        {"type", "cancelByCloid"},
        {"cancels", json::array({{{"a", "ETH"}, {"c", cloid}}})}
    }}};
    auto rsp = post_exchange(body);

    ASSERT_EQ(rsp.value("status", ""), "ok");
    EXPECT_EQ(rsp["response"]["data"]["statuses"][0].value("success", ""), cloid);

    uint64_t cursor = 0;
    auto [req, sz]  = request_queue_->read(cursor);
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->msg_type, MessageType::ORDER_CANCEL_REQUEST);
    EXPECT_STREQ(req->cancel_order.client_order_id, cloid.c_str());
}

// ===========================================================================
// Group C — Info endpoint
// ===========================================================================

// /info proxies to the real Hyperliquid API at base_url (see
// common/hyperliquid_info_proxy.hpp). The fixture's kTestBaseUrl is
// intentionally unreachable, so the proxy must never report success.
//
// This only checks that invariant, not the exact status/body: slick::net::Http
// has a hardcoded, non-configurable 30s timeout, so this test chains two
// independent 30s-timeout calls (this test's client -> our gateway -> the
// unreachable upstream) whose clocks race — the outer call can time out on
// its own before our gateway's inner proxy call finishes and writes its
// clean 502, making the exact observed status/body flaky. See
// HyperliquidInfoProxy in test_hyperliquid_info_proxy.cpp for deterministic,
// network-free coverage of the actual 502-vs-relay decision logic.
TEST_F(HyperliquidRestTest, InfoEndpoint_UnreachableUpstream_NeverReturnsOk) {
    auto rsp = Http::post(std::string(kServerUrl) + "/info",
                          json({{"type","meta"}}).dump());
    EXPECT_FALSE(rsp.is_ok());
}
