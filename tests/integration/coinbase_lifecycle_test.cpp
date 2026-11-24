// Each lifecycle test starts a separate slick-sim instance from the checked-in template, with any
// needed overrides. This isolates shutdown, heartbeats, initial socket sequence numbers and
// self-match prevention from each other and from the shared simulator tests.

#include "test_environment.hpp"

#include "harness/rest_probe.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <regex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using json = nlohmann::json;
using slick::sim::integration::env;

namespace {

bool startSim(slick::sim::integration::SimHarness& sim,
              const std::string& name,
              const json& overrides,
              std::string& error) {
    const auto& paths = env().paths();
    return sim.start(paths.sim_binary, paths.config_template,
                     (std::filesystem::path(paths.work_directory) / name).string(), overrides, error);
}

bool startClient(slick::sim::integration::ClientDriver& client,
                 const std::string& user,
                 const slick::sim::integration::SimHarness& sim,
                 const std::string& name,
                 std::string& error) {
    const auto& paths = env().paths();
    return client.start(paths.client_binary, user, sim.rest_port(), sim.ws_port(), sim.md_port(),
                        (std::filesystem::path(paths.work_directory) / name).string(), error);
}

bool waitReady(slick::sim::integration::ClientDriver& client) {
    return client.wait_for([](const json& event) { return event.value("event", "") == "ready"; }, 20s) != nullptr;
}

bool subscribe(slick::sim::integration::ClientDriver& client, const std::string& product) {
    if (!client.send({{"cmd", "md_subscribe"}, {"products", {product}}, {"channels", {"level2", "heartbeats"}}})) {
        return false;
    }
    return client.wait_for(
               [&](const json& event) {
                   return event.value("event", "") == "l2" && event.value("kind", "") == "snapshot" &&
                          event.value("product", "") == product;
               },
               10s) != nullptr;
}

json newOrder(slick::sim::integration::ClientDriver& client, json command) {
    client.clear_events();
    command["cmd"] = "new";
    EXPECT_TRUE(client.send(command));
    const auto* ack = client.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "new"; }, 10s);
    return ack == nullptr ? json::object() : *ack;
}

std::string dumpEvents(const std::vector<json>& events) { return json(events).dump(2); }

std::string restBase(const slick::sim::integration::SimHarness& sim) {
    return std::format("http://127.0.0.1:{}", sim.rest_port());
}

}  // namespace

TEST(CoinbaseLifecycleTest, CliShutdownStopsCleanlyAndReleasesItsPorts) {
    REQUIRE_ENVIRONMENT();
    slick::sim::integration::SimHarness sim;
    std::string error;
    ASSERT_TRUE(startSim(sim, "shutdown", json::object(), error)) << error;

    const auto targets = sim.cli(slick::sim::integration::CliCommand::LIST_TARGETS, "");
    EXPECT_TRUE(targets.ok) << targets.text;
    EXPECT_TRUE(targets.contains("sim")) << targets.text;

    const auto level = sim.cli(slick::sim::integration::CliCommand::SET_LOG_LEVEL, "sim", "debug");
    EXPECT_TRUE(level.ok) << level.text;
    EXPECT_TRUE(level.contains("log level DEBUG")) << level.text;

    const auto shutdown = sim.cli(slick::sim::integration::CliCommand::SHUTDOWN, "sim");
    EXPECT_TRUE(shutdown.ok) << shutdown.text;
    const auto code = sim.process().wait_for_exit(15s);
    ASSERT_TRUE(code.has_value()) << "the simulator did not exit after CLI shutdown";
    EXPECT_EQ(*code, 0);
    EXPECT_NE(sim.log().find("ExchangeSimulator stopped"), std::string::npos) << sim.log();

    // Every listening socket went with the process.
    EXPECT_FALSE(slick::sim::integration::port_is_open(sim.rest_port(), 250ms));
    EXPECT_FALSE(slick::sim::integration::port_is_open(sim.ws_port(), 250ms));
    EXPECT_FALSE(slick::sim::integration::port_is_open(sim.md_port(), 250ms));
    EXPECT_FALSE(slick::sim::integration::port_is_open(sim.cli_port(), 250ms));
}

TEST(CoinbaseLifecycleTest, HeartbeatsKeepAClientConnected) {
    REQUIRE_ENVIRONMENT();
    slick::sim::integration::SimHarness sim;
    std::string error;
    // A one-second ping with a two-second pong deadline: a client that never
    // answered pings would be closed long before the assertions below.
    ASSERT_TRUE(startSim(sim, "heartbeat",
                         json::parse(R"({"exchanges":{"coinbase":{"order_gateway":{"ws":
                             {"ping_interval":1,"pong_timeout":2}}}}})"),
                         error))
        << error;

    slick::sim::integration::ClientDriver alice;
    ASSERT_TRUE(startClient(alice, "alice", sim, "heartbeat", error)) << error;
    ASSERT_TRUE(waitReady(alice)) << alice.stderr_text();
    ASSERT_TRUE(subscribe(alice, "BTC-USD"));

    std::this_thread::sleep_for(6s);

    EXPECT_TRUE(alice.running()) << "the heartbeat check disconnected the client: " << alice.stderr_text();
    // A live WebSocket round trip: an order still reaches the engine.
    const auto ack = newOrder(alice, {{"product", "BTC-USD"}, {"side", "BUY"}, {"coid", "heartbeat-1"},
                                      {"qty", 0.001}, {"price", 90.00}});
    EXPECT_TRUE(ack.value("success", false)) << ack.dump();

    const std::string log = sim.log();
    EXPECT_EQ(log.find("failed heartbeat check"), std::string::npos) << log;
    EXPECT_EQ(log.find("Disconnected stale clients"), std::string::npos) << log;

    alice.quit();
    EXPECT_TRUE(sim.stop());
}

TEST(CoinbaseLifecycleTest, MarketDataStartsAtSequenceZeroAndServesTicker) {
    REQUIRE_ENVIRONMENT();
    slick::sim::integration::SimHarness sim;
    std::string error;
    ASSERT_TRUE(startSim(sim, "sequence", json::object(), error)) << error;

    slick::sim::integration::ClientDriver alice;
    ASSERT_TRUE(startClient(alice, "alice", sim, "sequence", error)) << error;
    ASSERT_TRUE(waitReady(alice)) << alice.stderr_text();

    // The first message on a new socket must have sequence zero. An uninitialized counter
    // previously produced arbitrary values that clients reported as sequence gaps.
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "md_subscribe"},
                            {"products", {"BTC-USD"}},
                            {"channels", {"level2", "ticker"}}}));
    ASSERT_TRUE(alice.wait_for(
        [](const json& event) {
            return event.value("event", "") == "l2" && event.value("kind", "") == "snapshot";
        },
        10s) != nullptr)
        << dumpEvents(alice.events());
    alice.poll();

    const json* first_market_data = nullptr;
    for (const auto& event : alice.events()) {
        const auto kind = event.value("event", "");
        if ((kind == "l2" || kind == "trades" || kind == "ticker") && !first_market_data) {
            first_market_data = &event;
        }
    }
    ASSERT_NE(first_market_data, nullptr);
    // The client subscribes to market-data heartbeats at startup. Its confirmation uses sequence 0,
    // and the l2 snapshot follows one or two messages later. The old uninitialized counter could
    // produce an arbitrary 64-bit value here and trigger a gap report.
    EXPECT_LE(first_market_data->value("seq", 999999u), 4u) << dumpEvents(alice.events());

    // The ticker channel has no simulated quotes. It confirms the subscription with an empty
    // snapshot instead of an error.
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "md_subscribe"}, {"products", {"BTC-USD"}}, {"channels", {"ticker"}}}));
    const auto* ticker = alice.wait_for(
        [](const json& event) {
            return event.value("event", "") == "ticker" && event.value("kind", "") == "snapshot";
        },
        10s);
    ASSERT_NE(ticker, nullptr) << dumpEvents(alice.events());
    EXPECT_EQ(ticker->value("count", 99), 0);

    alice.quit();
    EXPECT_TRUE(sim.stop());
}

TEST(CoinbaseLifecycleTest, SelfMatchPreventionRejectsTheNewestOrder) {
    REQUIRE_ENVIRONMENT();
    slick::sim::integration::SimHarness sim;
    std::string error;
    ASSERT_TRUE(startSim(sim, "smp",
                         json::parse(R"({"exchanges":{"coinbase":{"self_match_prevention":"cancel_newest"}}})"),
                         error))
        << error;

    slick::sim::integration::ClientDriver alice;
    ASSERT_TRUE(startClient(alice, "alice", sim, "smp", error)) << error;
    ASSERT_TRUE(waitReady(alice)) << alice.stderr_text();
    ASSERT_TRUE(subscribe(alice, "BTC-USD"));

    const auto resting = newOrder(alice, {{"product", "BTC-USD"}, {"side", "BUY"}, {"coid", "smp-resting"},
                                          {"qty", 0.001}, {"price", 40.00}});
    ASSERT_TRUE(resting.value("success", false)) << resting.dump();

    // Let the resting order reach the book before the crossing order tries it.
    const auto* restingOpen = alice.wait_for(
        [](const json& event) {
            return event.value("event", "") == "order" && event.value("client_order_id", "") == "smp-resting" &&
                   event.value("status", "") == "OPEN";
        },
        10s);
    ASSERT_NE(restingOpen, nullptr) << dumpEvents(alice.events());

    // The crossing order goes through the SDK like any other. Its HTTP reply is the
    // PENDING_NEW acknowledgement, so the outcome to assert is the user-channel
    // report: one FAILED report and no fill.
    const auto crossing = newOrder(alice, {{"product", "BTC-USD"}, {"side", "SELL"}, {"coid", "smp-crossing"},
                                           {"qty", 0.001}, {"price", 40.00}});
    (void)crossing;
    const auto* rejected = alice.wait_for(
        [](const json& event) {
            return event.value("event", "") == "order" && event.value("client_order_id", "") == "smp-crossing" &&
                   event.value("status", "") == "FAILED";
        },
        10s);
    ASSERT_NE(rejected, nullptr) << dumpEvents(alice.events());
    EXPECT_EQ((*rejected).value("terminal", false), true);
    // The SDK does not expose reject_reason. Check the venue log to confirm the reject was SMP.
    EXPECT_TRUE(sim.wait_for_log("SMP")) << sim.log();

    // No trade happened: the resting order is still working.
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "orders"}, {"product", "BTC-USD"}, {"status", "OPEN"}}));
    const auto* listing = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "orders"; }, 10s);
    ASSERT_NE(listing, nullptr) << dumpEvents(alice.events());
    bool resting_alive = false;
    for (const auto& order : listing->at("orders")) {
        resting_alive = resting_alive || order.value("client_order_id", "") == "smp-resting";
    }
    EXPECT_TRUE(resting_alive) << listing->dump();
    EXPECT_GE(listing->value("count", 0), 1) << listing->dump();

    // Repricing the resting sell onto its owner's bid must produce an SMP reject and leave the sell
    // working. The REJECTED report previously removed routing, so later cancels returned
    // UNKNOWN_ORDER even though the order remained on the book.
    const auto amended = newOrder(alice, {{"product", "BTC-USD"}, {"side", "SELL"},
                                          {"coid", "smp-edit"}, {"qty", 0.001}, {"price", 42.00}});
    ASSERT_TRUE(amended.value("success", false)) << amended.dump();
    const std::string amended_id = amended.value("order_id", "");
    ASSERT_FALSE(amended_id.empty()) << amended.dump();

    alice.clear_events();
    const size_t log_before_edit = sim.log().size();
    ASSERT_TRUE(alice.send({{"cmd", "edit"}, {"order_id", amended_id}, {"product", "BTC-USD"},
                            {"price", 40.00}, {"size", 0.001}}));
    const auto* editAck = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "edit"; },
        10s);
    ASSERT_NE(editAck, nullptr) << dumpEvents(alice.events());

    // The edit path reports PENDING_REPLACE before the engine's verdict, and the gateway's
    // synchronous poll stops at that first report. Check the venue log for the reject. Its empty
    // client_order_id distinguishes an amendment reject from the earlier new-order reject. The WS
    // gateway logs it from its own thread after the REST reply may already be back, so wait for it.
    EXPECT_TRUE(sim.wait_for_log("REJECTED to user_id=alice, client_order_id=", log_before_edit))
        << "the crossing amendment was not rejected: " << sim.log().substr(log_before_edit);

    // The order is still working, so it must still be cancellable by id.
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "cancel"}, {"order_ids", json::array({amended_id})}}));
    const auto* cancelAck = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "cancel"; },
        10s);
    ASSERT_NE(cancelAck, nullptr) << dumpEvents(alice.events());
    ASSERT_EQ(cancelAck->at("results").size(), 1u) << cancelAck->dump();
    EXPECT_TRUE((*cancelAck)["results"][0].value("success", false))
        << "a rejected amendment left the order uncancellable: " << cancelAck->dump();

    alice.quit();
    EXPECT_TRUE(sim.stop());
}

// An order accepted after its client disconnects must remain routable. The gateway learns the
// symbol from the report stream, which must be drained even with no HTTP waiter. Previously, the
// report remained unread until a later request drained it, and cancels returned UNKNOWN_ORDER.
TEST(CoinbaseLifecycleTest, AnOrderIsRoutableAfterItsClientDisconnectsMidRequest) {
    REQUIRE_ENVIRONMENT();
    slick::sim::integration::SimHarness sim;
    std::string error;
    // A response poll of a second puts the cancel far ahead of the first tick, so the
    // lookup below cannot be served by the timer: it has to drain the report itself.
    ASSERT_TRUE(startSim(sim, "disconnect",
                         json::parse(R"({"exchanges":{"coinbase":{"order_gateway":{"rest":{"response_poll_ms":1000}}}}})"), error))
        << error;

    slick::sim::integration::ClientDriver alice;
    ASSERT_TRUE(startClient(alice, "alice", sim, "disconnect", error)) << error;
    ASSERT_TRUE(waitReady(alice)) << alice.stderr_text();
    ASSERT_TRUE(subscribe(alice, "BTC-USD"));

    // The SDK always waits for its reply, so this request goes out over a bare socket
    // that hangs up as soon as the body is on its way.
    const json body = {
        {"client_order_id", "disc-1"},
        {"product_id", "BTC-USD"},
        {"side", "BUY"},
        {"order_configuration", {{"limit_limit_gtc", {{"base_size", "0.001"}, {"limit_price", "20.00"}}}}},
    };
    const slick::sim::integration::HttpHeaders auth = {
        {"Authorization", "Bearer " + slick::sim::integration::bearerToken("alice")}};
    // Hang up once the exchange has accepted the order, long before the one-second poll sends the
    // reply. A close straight after the send loses the request on macOS (see httpPostAndHangUp).
    ASSERT_TRUE(slick::sim::integration::httpPostAndHangUp(
        std::format("127.0.0.1:{}", sim.rest_port()), "/api/v3/brokerage/orders", body.dump(), auth,
        [&sim] { sim.wait_for_log("client_oid=disc-1, price="); }))
        << "the raw POST never reached the simulator";

    // Read the generated order id from the venue's acceptance log. A listing would drain the report
    // stream and hide the routing bug.
    const std::regex accepted(R"(Adding order \d+: oid=([0-9a-fA-F-]{36}), client_oid=disc-1)");
    const std::string log = sim.log();
    std::smatch match;
    ASSERT_TRUE(std::regex_search(log, match, accepted)) << "the simulator never accepted the order: " << log;
    const std::string order_id = match[1];

    // Let the exchange publish the report; the poll is still most of a second away.
    std::this_thread::sleep_for(20ms);

    // No listing before this: a cancel is the first thing to ask for routing.
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "cancel"}, {"order_ids", json::array({order_id})}}));
    const auto* ack = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "cancel"; },
        10s);
    ASSERT_NE(ack, nullptr) << dumpEvents(alice.events());
    ASSERT_EQ(ack->at("results").size(), 1u) << ack->dump();
    EXPECT_TRUE((*ack)["results"][0].value("success", false))
        << "a disconnecting client's order was not routable: " << ack->dump();

    alice.quit();
    EXPECT_TRUE(sim.stop());
}
