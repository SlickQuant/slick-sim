// End-to-end coverage for a Coinbase-only simulator without a live feed. Tests drive slick-sim
// through its public ports. Three separate clients use the coinbase-advanced-cpp SDK
// (tests/integration/coinbase_client.cpp), and raw HTTP probes cover requests the SDK cannot
// express, such as missing Authorization headers.
//
// Each test uses its own product to avoid declaration or link-order dependencies: BTC-USD for FIFO
// and market data, ETH-USD for partial fills and edits, SOL-USD for TIF and REST listing, and
// LTC-USD for orders submitted before a subscription.

#include "test_environment.hpp"

#include "harness/rest_probe.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using json = nlohmann::json;
using slick::sim::integration::env;
using slick::sim::integration::HttpHeaders;

namespace {

std::string dumpEvents(const std::vector<json>& events);

std::string restBase() {
    return std::format("http://127.0.0.1:{}", env().sim().rest_port());
}

HttpHeaders auth(const std::string& user = "alice") {
    return {{"Authorization", "Bearer " + slick::sim::integration::bearerToken(user)}};
}

json limitOrder(const std::string& product, const std::string& side, const std::string& coid,
                const std::string& size, const std::string& price) {
    return {{"product_id", product},
            {"side", side},
            {"client_order_id", coid},
            {"order_configuration",
             {{"limit_limit_gtc", {{"base_size", size}, {"limit_price", price}, {"post_only", false}}}}}};
}

/// Subscribes a client to its product's book and trades, and waits for the
/// snapshot. Symbols exist only after this call (docs/architecture.md).
bool subscribeBook(slick::sim::integration::ClientDriver& client, const std::string& product) {
    if (!client.send({{"cmd", "md_subscribe"}, {"products", {product}}, {"channels", {"level2", "market_trades"}}})) {
        return false;
    }
    return client.wait_for(
               [&](const json& event) {
                   return event.value("event", "") == "l2" && event.value("kind", "") == "snapshot" &&
                          event.value("product", "") == product;
               },
               10s) != nullptr;
}

/// Submits a new order and returns the client's ack (copied: the driver's storage
/// moves when it polls again).
json newOrder(slick::sim::integration::ClientDriver& client, json command) {
    client.clear_events();
    command["cmd"] = "new";
    EXPECT_TRUE(client.send(command));
    const auto* ack = client.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "new"; }, 10s);
    if (ack == nullptr) {
        ADD_FAILURE() << client.user() << " never acknowledged " << command.dump();
        return json::object();
    }
    return *ack;
}

/// Waits for a user-channel report for `coid` whose status is `status`.
json waitForOrder(slick::sim::integration::ClientDriver& client,
                  const std::string& coid,
                  const std::string& status,
                  std::chrono::milliseconds timeout = 10s) {
    const auto* event = client.wait_for(
        [&](const json& candidate) {
            return candidate.value("event", "") == "order" && candidate.value("client_order_id", "") == coid &&
                   candidate.value("status", "") == status;
        },
        timeout);
    if (event == nullptr) {
        ADD_FAILURE() << client.user() << " never reported " << coid << " as " << status << "\nevents:\n"
                      << dumpEvents(client.events());
        return json::object();
    }
    return *event;
}

}  // namespace

// ===========================================================================
// Startup and configuration
// ===========================================================================

TEST(CoinbaseStartupTest, RunsCoinbaseOnlyWithNoLiveFeed) {
    REQUIRE_ENVIRONMENT();
    auto& sim = env().sim();

    ASSERT_TRUE(sim.running()) << "the simulator exited; log:\n" << sim.log();

    const std::string log = sim.log();
    // The log line lists the adapters compiled into the binary. The config can enable a different
    // set; this test permits only Coinbase to be instantiated.
    const auto venue_line = log.find("Venue adapters built in:");
    ASSERT_NE(venue_line, std::string::npos) << log;
    const auto venue_end = log.find('\n', venue_line);
    const std::string venues = log.substr(venue_line, venue_end - venue_line);
    EXPECT_NE(venues.find("coinbase"), std::string::npos) << venues;
    EXPECT_EQ(log.find("Exchange HYPERLIQUID"), std::string::npos) << "a Coinbase-only config started Hyperliquid";
    EXPECT_NE(log.find("COINBASE REST OrderGateway started on port"), std::string::npos) << log;
    EXPECT_NE(log.find("COINBASE WS OrderGateway started on port"), std::string::npos) << log;
    EXPECT_NE(log.find("COINBASE Publisher started on port"), std::string::npos) << log;
    EXPECT_NE(log.find("CLI listening on port"), std::string::npos) << log;
    // A live feed writes a raw capture here; a self-contained run never does.
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(sim.work_directory()) / "logs" / "coinbase_data"));
    // The only outbound calls the adapter can make are the product list and the
    // taker fee rate. `initial_products` and `fee_rate` in the template remove both.
    EXPECT_EQ(log.find("api.coinbase.com"), std::string::npos) << "the simulator reached the real venue:\n" << log;
}

TEST(CoinbaseStartupTest, ConfigIsCoinbaseOnlyAndFeedFree) {
    REQUIRE_ENVIRONMENT();
    const std::string config = slick::sim::integration::read_file(env().sim().config_path());
    ASSERT_FALSE(config.empty());
    const auto parsed = json::parse(config);
    EXPECT_FALSE(parsed["exchanges"].contains("hyperliquid")) << "a Coinbase-only config must not name Hyperliquid";
    EXPECT_FALSE(parsed["exchanges"]["coinbase"].contains("md_feeds")) << "the run must not subscribe to a live feed";
}


// ===========================================================================
// REST order entry: products, authentication, validation
// ===========================================================================

TEST(CoinbaseRestTest, ProductsAreServedFromTheConfiguredList) {
    REQUIRE_ENVIRONMENT();
    for (const std::string path : {"/api/v3/brokerage/market/products", "/api/v3/brokerage/products"}) {
        const auto response = slick::sim::integration::httpGet(restBase() + path);
        ASSERT_EQ(response.status, 200u) << path << ": " << response.body;
        const auto products = json::parse(response.body)["products"];
        bool found = false;
        for (const auto& product : products) {
            found = found || product.value("product_id", "") == "BTC-USD";
        }
        EXPECT_TRUE(found) << path;
    }

    const auto single = slick::sim::integration::httpGet(restBase() + "/api/v3/brokerage/market/products/ETH-USD");
    ASSERT_EQ(single.status, 200u) << single.body;
    EXPECT_EQ(json::parse(single.body)["product_id"], "ETH-USD");

    const auto unknown = slick::sim::integration::httpGet(restBase() + "/api/v3/brokerage/market/products/NOPE-USD");
    EXPECT_EQ(unknown.status, 400u);
    EXPECT_TRUE(unknown.contains("INVALID_ARGUMENT")) << unknown.body;
}

TEST(CoinbaseRestTest, OrderEntryRequiresAWellFormedJwt) {
    REQUIRE_ENVIRONMENT();
    const std::string url = restBase() + "/api/v3/brokerage/orders";
    const std::string body = limitOrder("BTC-USD", "BUY", "authless", "0.001", "1.00").dump();

    const auto missing = slick::sim::integration::httpPost(url, body, {{"Content-Type", "application/json"}});
    EXPECT_EQ(missing.status, 401u) << missing.body;

    const auto malformed = slick::sim::integration::httpPost(
        url, body, {{"Content-Type", "application/json"}, {"Authorization", "Bearer not-a-jwt"}});
    EXPECT_EQ(malformed.status, 401u) << malformed.body;

    // The gateway accepts syntactically valid tokens without checking signatures. An
    // UNKNOWN_CONTRACT response confirms the request passed authentication, crossed the queue and
    // reached the matching engine; an authentication failure would return 401.
    const auto accepted = slick::sim::integration::httpPost(
        url, limitOrder("LTC-USD", "BUY", "alice-ltc-1", "0.001", "1.00").dump(),
        {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + slick::sim::integration::bearerToken("alice")}});
    ASSERT_EQ(accepted.status, 400u) << accepted.body;
    EXPECT_TRUE(accepted.contains("UNKNOWN_CONTRACT")) << accepted.body;

    // The reject must reach both the HTTP caller and the token user's user channel.
    const auto rejected = waitForOrder(env().client("alice"), "alice-ltc-1", "FAILED");
    EXPECT_EQ(rejected.value("terminal", false), true);
}

TEST(CoinbaseRestTest, ValidationRejectsBeforeQueueing) {
    REQUIRE_ENVIRONMENT();
    const std::string url = restBase() + "/api/v3/brokerage/orders";
    const auto post = [&](const std::string& body) {
        return slick::sim::integration::httpPost(
            url, body, {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + slick::sim::integration::bearerToken("alice")}});
    };

    const auto unknown_product = post(limitOrder("DOGE-USD", "BUY", "val-1", "1", "1.00").dump());
    EXPECT_EQ(unknown_product.status, 400u);
    EXPECT_TRUE(unknown_product.contains("INVALID_PRODUCT_ID")) << unknown_product.body;

    const auto off_tick = post(limitOrder("BTC-USD", "BUY", "val-2", "0.001", "100.005").dump());
    EXPECT_EQ(off_tick.status, 400u);
    EXPECT_TRUE(off_tick.contains("INVALID_LIMIT_PRICE_POST_ONLY")) << off_tick.body;

    const auto too_small = post(limitOrder("BTC-USD", "BUY", "val-3", "0.000000001", "100.00").dump());
    EXPECT_EQ(too_small.status, 400u);
    EXPECT_TRUE(too_small.contains("INVALID_ORDER_CONFIG")) << too_small.body;

    const auto unsupported = post(json{{"product_id", "BTC-USD"},
                                       {"side", "BUY"},
                                       {"client_order_id", "val-4"},
                                       {"order_configuration", {{"limit_limit_gtd", json::object()}}}}
                                      .dump());
    EXPECT_EQ(unsupported.status, 400u);
    EXPECT_TRUE(unsupported.contains("Unsupported order_configuration type")) << unsupported.body;

    const auto malformed = post("{not json");
    EXPECT_EQ(malformed.status, 400u);
    EXPECT_TRUE(malformed.contains("Invalid JSON")) << malformed.body;
}


// ===========================================================================
// Trading: a market built by clients, FIFO fills, and market data
// ===========================================================================

namespace {

/// Applies the client's level2 stream so assertions check the subscriber's view independently of
/// the simulator's internal state.
class BookMirror {
public:
    void apply(const std::vector<json>& events, const std::string& product) {
        for (const auto& event : events) {
            if (event.value("event", "") != "l2" || event.value("product", "") != product) {
                continue;
            }
            if (event.value("kind", "") == "snapshot") {
                bids_.clear();
                offers_.clear();
            }
            for (const auto& update : event.value("updates", json::array())) {
                auto& side = update.value("side", "") == "bid" ? bids_ : offers_;
                const double price = update.value("price", 0.0);
                const double qty = update.value("qty", 0.0);
                if (qty <= 0.0) {
                    side.erase(price);
                } else {
                    side[price] = qty;
                }
            }
        }
    }

    double qtyAt(bool bid, double price) const {
        const auto& side = bid ? bids_ : offers_;
        const auto it = side.find(price);
        return it == side.end() ? 0.0 : it->second;
    }

    double bestBid() const { return bids_.empty() ? 0.0 : bids_.rbegin()->first; }
    double bestOffer() const { return offers_.empty() ? 0.0 : offers_.begin()->first; }

private:
    std::map<double, double> bids_;
    std::map<double, double> offers_;
};

std::string dumpEvents(const std::vector<json>& events) { return json(events).dump(2); }

std::vector<json> tradesIn(const std::vector<json>& events) {
    std::vector<json> trades;
    for (const auto& event : events) {
        if (event.value("event", "") != "trades") {
            continue;
        }
        for (const auto& trade : event.value("trades", json::array())) {
            trades.push_back(trade);
        }
    }
    return trades;
}

/// Waits for a user-channel report satisfying `predicate`, for cases where the
/// status alone matches more than one state (a resting order's NEW ack is OPEN
/// too, before it has traded).
json waitForOrderWhere(slick::sim::integration::ClientDriver& client,
                       std::function<bool(const json&)> predicate,
                       std::string_view what,
                       std::chrono::milliseconds timeout = 10s) {
    const auto* event = client.wait_for(predicate, timeout);
    if (event == nullptr) {
        ADD_FAILURE() << client.user() << " never reported " << what << "events:"
                      << dumpEvents(client.events());
        return json::object();
    }
    return *event;
}

/// Collects market-data trades until `count` have arrived, or the deadline passes.
std::vector<json> waitForTrades(slick::sim::integration::ClientDriver& client,
                                size_t count,
                                std::chrono::milliseconds timeout = 10s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<json> trades;
    for (;;) {
        client.poll();
        trades = tradesIn(client.events());
        if (trades.size() >= count || std::chrono::steady_clock::now() >= deadline) {
            return trades;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/// Waits for a level to be published with quantity zero on `side` at `price`.
void waitForLevelDelete(slick::sim::integration::ClientDriver& client,
                        const std::string& product,
                        const std::string& side,
                        double price,
                        std::chrono::milliseconds timeout = 10s) {
    const bool seen = client.wait_for(
                          [&](const json& event) {
                              if (event.value("event", "") != "l2" || event.value("product", "") != product) {
                                  return false;
                              }
                              for (const auto& update : event.value("updates", json::array())) {
                                  if (update.value("side", "") == side && update.value("price", 0.0) == price &&
                                      update.value("qty", 1.0) == 0.0) {
                                      return true;
                                  }
                              }
                              return false;
                          },
                          timeout) != nullptr;
    EXPECT_TRUE(seen) << "no delete for " << side << " " << price << " on " << product << " "
                      << dumpEvents(client.events());
}

}  // namespace

TEST(CoinbaseTradingTest, ClientsBuildAMarketAndTradeUnderFifo) {
    REQUIRE_ENVIRONMENT();
    auto& alice = env().client("alice");
    auto& bob = env().client("bob");
    auto& carol = env().client("carol");
    const std::string product = "BTC-USD";

    ASSERT_TRUE(subscribeBook(alice, product));
    ASSERT_TRUE(subscribeBook(bob, product));
    ASSERT_TRUE(subscribeBook(carol, product));

    // Alice quotes both sides; Bob joins her offer at the same price, so FIFO
    // decides who an incoming buyer fills first.
    ASSERT_TRUE(newOrder(alice, {{"product", product}, {"side", "BUY"}, {"coid", "alice-bid"},
                                 {"qty", 0.001}, {"price", 99.00}})
                    .value("success", false));
    ASSERT_TRUE(newOrder(alice, {{"product", product}, {"side", "SELL"}, {"coid", "alice-ask"},
                                 {"qty", 0.001}, {"price", 100.00}})
                    .value("success", false));
    ASSERT_TRUE(newOrder(bob, {{"product", product}, {"side", "SELL"}, {"coid", "bob-ask"},
                               {"qty", 0.001}, {"price", 100.00}})
                    .value("success", false));

    // Carol has only subscribed to market data so far. Her two-sided book includes the other
    // clients' liquidity.
    carol.poll();
    BookMirror mirror;
    mirror.apply(carol.events(), product);
    EXPECT_DOUBLE_EQ(mirror.qtyAt(true, 99.00), 0.001);
    EXPECT_DOUBLE_EQ(mirror.qtyAt(false, 100.00), 0.002);
    EXPECT_DOUBLE_EQ(mirror.bestBid(), 99.00);
    EXPECT_DOUBLE_EQ(mirror.bestOffer(), 100.00);

    // IOC for more than the book holds: 0.002 fills, 0.0005 is cancelled.
    const auto take = newOrder(carol, {{"product", product}, {"side", "BUY"}, {"tif", "IOC"},
                                       {"coid", "carol-take"}, {"qty", 0.0025}, {"price", 100.00}});
    ASSERT_TRUE(take.value("success", false)) << take.dump();

    // FIFO: Alice's offer rested first and is filled completely, then Bob's.
    const auto aliceFill = waitForOrder(alice, "alice-ask", "FILLED");
    EXPECT_DOUBLE_EQ(aliceFill.value("cum_qty", 0.0), 0.001);
    EXPECT_DOUBLE_EQ(aliceFill.value("leaves_qty", 1.0), 0.0);
    EXPECT_DOUBLE_EQ(aliceFill.value("avg_price", 0.0), 100.00);
    EXPECT_DOUBLE_EQ(aliceFill.value("completion", 0.0), 100.0);
    EXPECT_EQ(aliceFill.value("tif", ""), "GOOD_UNTIL_CANCELLED");
    EXPECT_EQ(aliceFill.value("side", ""), "SELL");
    EXPECT_EQ(aliceFill.value("type", ""), "LIMIT");

    const auto bobFill = waitForOrder(bob, "bob-ask", "FILLED");
    EXPECT_DOUBLE_EQ(bobFill.value("cum_qty", 0.0), 0.001);

    // Carol's remainder is cancelled by the IOC; the partial state she saw was
    // OPEN with the unfilled quantity still live.
    const auto carolDone = waitForOrder(carol, "carol-take", "CANCELLED");
    EXPECT_DOUBLE_EQ(carolDone.value("cum_qty", 0.0), 0.002);
    EXPECT_DOUBLE_EQ(carolDone.value("leaves_qty", 1.0), 0.0);
    EXPECT_EQ(carolDone.value("terminal", false), true);

    // Both fills must report OPEN before cancellation because quantity remains live. A FILLED
    // report would incorrectly mark the order complete.
    bool sawOpen = false;
    for (const auto& event : carol.events()) {
        if (event.value("event", "") == "order" && event.value("client_order_id", "") == "carol-take" &&
            event.value("status", "") == "OPEN") {
            sawOpen = true;
        }
    }
    EXPECT_TRUE(sawOpen) << dumpEvents(carol.events());

    // The public tape has one print per match summary. Sweeping two resting orders at one price
    // produces one print for the total quantity, while each resting order gets its own user-channel
    // fill report, checked above. The venue assigns the same trade id to both sockets.
    const auto carolTrades = waitForTrades(carol, 1);
    ASSERT_EQ(carolTrades.size(), 1u) << dumpEvents(carol.events());
    EXPECT_EQ(carolTrades[0].value("side", ""), "BUY") << "side is the aggressor's";
    EXPECT_DOUBLE_EQ(carolTrades[0].value("price", 0.0), 100.00);
    EXPECT_DOUBLE_EQ(carolTrades[0].value("size", 0.0), 0.002);
    const auto aliceTrades = waitForTrades(alice, 1);
    ASSERT_FALSE(aliceTrades.empty());
    std::vector<std::string> aliceIds;
    for (const auto& trade : aliceTrades) {
        aliceIds.push_back(trade.value("trade_id", ""));
    }
    for (const auto& trade : carolTrades) {
        EXPECT_NE(std::find(aliceIds.begin(), aliceIds.end(), trade.value("trade_id", "")), aliceIds.end())
            << "a trade id must be stable across clients";
    }

    // The whole offer level is gone; the bid is untouched.
    waitForLevelDelete(carol, product, "offer", 100.00);
    mirror.apply(carol.events(), product);
    EXPECT_DOUBLE_EQ(mirror.qtyAt(false, 100.00), 0.0);
    EXPECT_DOUBLE_EQ(mirror.qtyAt(true, 99.00), 0.001);
    EXPECT_DOUBLE_EQ(mirror.bestOffer(), 0.0);
}


TEST(CoinbaseTradingTest, PartialFillThenCancelAndEdit) {
    REQUIRE_ENVIRONMENT();
    auto& alice = env().client("alice");
    auto& bob = env().client("bob");
    auto& carol = env().client("carol");
    const std::string product = "ETH-USD";

    ASSERT_TRUE(subscribeBook(alice, product));
    ASSERT_TRUE(subscribeBook(bob, product));
    ASSERT_TRUE(subscribeBook(carol, product));

    const auto rest = newOrder(alice, {{"product", product}, {"side", "BUY"}, {"coid", "eth-bid"},
                                       {"qty", 0.004}, {"price", 50.00}});
    ASSERT_TRUE(rest.value("success", false)) << rest.dump();
    ASSERT_TRUE(newOrder(bob, {{"product", product}, {"side", "SELL"}, {"tif", "IOC"}, {"coid", "eth-hit"},
                               {"qty", 0.001}, {"price", 50.00}})
                    .value("success", false));

    // A partly filled resting order stays OPEN with its remainder live. The NEW
    // ack is OPEN as well, so the fill has to be part of the match.
    const auto partial = waitForOrderWhere(
        alice,
        [](const json& event) {
            return event.value("event", "") == "order" && event.value("client_order_id", "") == "eth-bid" &&
                   event.value("status", "") == "OPEN" && event.value("cum_qty", 0.0) > 0.0;
        },
        "eth-bid partially filled");
    EXPECT_DOUBLE_EQ(partial.value("cum_qty", 0.0), 0.001);
    EXPECT_DOUBLE_EQ(partial.value("leaves_qty", 0.0), 0.003);
    EXPECT_LT(partial.value("completion", 100.0), 100.0);

    // Cancel the remainder through the REST endpoint by simulator order id.
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "cancel"}, {"order_ids", {rest.value("order_id", "")}}}));
    const auto* cancelAck = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "cancel"; },
        10s);
    ASSERT_NE(cancelAck, nullptr) << dumpEvents(alice.events());
    ASSERT_FALSE(cancelAck->value("results", json::array()).empty()) << cancelAck->dump();
    EXPECT_TRUE((*cancelAck)["results"][0].value("success", false)) << cancelAck->dump();

    const auto cancelled = waitForOrder(alice, "eth-bid", "CANCELLED");
    EXPECT_DOUBLE_EQ(cancelled.value("cum_qty", 0.0), 0.001);
    EXPECT_DOUBLE_EQ(cancelled.value("leaves_qty", 1.0), 0.0);

    // The level the cancelled order rested on is withdrawn from the published book.
    waitForLevelDelete(carol, product, "bid", 50.00);
    BookMirror mirror;
    mirror.apply(carol.events(), product);
    EXPECT_DOUBLE_EQ(mirror.qtyAt(true, 50.00), 0.0);
    EXPECT_DOUBLE_EQ(mirror.bestBid(), 0.0);

    // An amendment changes the price and size; it then trades at the new price.
    const auto edited = newOrder(alice, {{"product", product}, {"side", "BUY"}, {"coid", "eth-edit"},
                                         {"qty", 0.001}, {"price", 49.00}});
    ASSERT_TRUE(edited.value("success", false)) << edited.dump();
    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "edit"},
                            {"order_id", edited.value("order_id", "")},
                            {"product", product},
                            {"price", 49.50},
                            {"size", 0.002}}));
    const auto* editAck = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "edit"; }, 10s);
    ASSERT_NE(editAck, nullptr) << dumpEvents(alice.events());
    EXPECT_TRUE(editAck->value("success", false)) << editAck->dump();

    const auto replaced = waitForOrderWhere(
        alice,
        [](const json& event) {
            return event.value("event", "") == "order" && event.value("client_order_id", "") == "eth-edit" &&
                   event.value("status", "") == "OPEN" && event.value("limit_price", 0.0) == 49.50;
        },
        "eth-edit replaced at 49.50");
    EXPECT_DOUBLE_EQ(replaced.value("limit_price", 0.0), 49.50);

    ASSERT_TRUE(newOrder(bob, {{"product", product}, {"side", "SELL"}, {"tif", "IOC"}, {"coid", "eth-edit-hit"},
                               {"qty", 0.002}, {"price", 49.50}})
                    .value("success", false));
    const auto filled = waitForOrder(alice, "eth-edit", "FILLED");
    EXPECT_DOUBLE_EQ(filled.value("cum_qty", 0.0), 0.002);
    EXPECT_DOUBLE_EQ(filled.value("avg_price", 0.0), 49.50);
}


TEST(CoinbaseTradingTest, TimeInForceBatchCancelAndHistoricalListing) {
    REQUIRE_ENVIRONMENT();
    auto& alice = env().client("alice");
    auto& bob = env().client("bob");
    auto& carol = env().client("carol");
    const std::string product = "SOL-USD";

    ASSERT_TRUE(subscribeBook(alice, product));
    ASSERT_TRUE(subscribeBook(bob, product));
    ASSERT_TRUE(subscribeBook(carol, product));

    // FOK that cannot fill completely is rejected and trades nothing.
    const auto resting = newOrder(alice, {{"product", product}, {"side", "BUY"}, {"coid", "sol-bid"},
                                          {"qty", 0.001}, {"price", 30.00}});
    ASSERT_TRUE(resting.value("success", false)) << resting.dump();

    const auto fokUnfilled = newOrder(bob, {{"product", product}, {"side", "SELL"}, {"tif", "FOK"},
                                           {"coid", "sol-fok-fail"}, {"qty", 0.002}, {"price", 30.00}});
    // The REST PENDING_NEW acknowledgement (docs/integration-coinbase.md) precedes the fill result.
    // An unfillable FOK is cancelled with no fills under docs/matching-engine.md; it must be
    // neither rejected nor partially filled.
    EXPECT_TRUE(fokUnfilled.value("success", true)) << fokUnfilled.dump();
    const auto cancelledFok = waitForOrder(bob, "sol-fok-fail", "CANCELLED");
    EXPECT_DOUBLE_EQ(cancelledFok.value("cum_qty", 1.0), 0.0);
    EXPECT_EQ(cancelledFok.value("terminal", false), true);

    // The same FOK, sized exactly to the resting quantity, fills.
    ASSERT_TRUE(newOrder(bob, {{"product", product}, {"side", "SELL"}, {"tif", "FOK"}, {"coid", "sol-fok-ok"},
                               {"qty", 0.001}, {"price", 30.00}})
                    .value("success", false));
    waitForOrder(bob, "sol-fok-ok", "FILLED");
    waitForOrder(alice, "sol-bid", "FILLED");

    // A market order sweeps two price levels.
    ASSERT_TRUE(newOrder(alice, {{"product", product}, {"side", "SELL"}, {"coid", "sol-ask-31"},
                                 {"qty", 0.001}, {"price", 31.00}})
                    .value("success", false));
    ASSERT_TRUE(newOrder(alice, {{"product", product}, {"side", "SELL"}, {"coid", "sol-ask-32"},
                                 {"qty", 0.001}, {"price", 32.00}})
                    .value("success", false));
    carol.clear_events();
    const auto market = newOrder(carol, {{"product", product}, {"side", "BUY"}, {"type", "MARKET"},
                                         {"tif", "IOC"}, {"coid", "sol-market"}, {"qty", 0.002}});
    ASSERT_TRUE(market.value("success", false)) << market.dump();
    const auto swept = waitForOrder(carol, "sol-market", "FILLED");
    EXPECT_DOUBLE_EQ(swept.value("cum_qty", 0.0), 0.002);
    EXPECT_EQ(swept.value("type", ""), "MARKET");

    carol.poll();
    std::vector<double> prices;
    for (const auto& trade : tradesIn(carol.events())) {
        prices.push_back(trade.value("price", 0.0));
    }
    std::sort(prices.begin(), prices.end());
    EXPECT_EQ(prices, (std::vector<double>{31.00, 32.00})) << dumpEvents(carol.events());

    // Cancel four known orders and one unknown id in a single request. The old 200 ms per-order
    // poll took about a second; the lower latency bound checks that regression.
    std::vector<std::string> order_ids;
    for (int i = 0; i < 4; ++i) {
        const auto ack = newOrder(alice, {{"product", product}, {"side", "BUY"},
                                          {"coid", std::format("sol-rest-{}", i)}, {"qty", 0.001},
                                          {"price", 29.00 + i * 0.10}});
        ASSERT_TRUE(ack.value("success", false)) << ack.dump();
        order_ids.push_back(ack.value("order_id", ""));
    }
    order_ids.push_back("00000000-0000-0000-0000-000000000000");
    // Reject a malformed entry before submitting any cancels. Previously, conversion threw after
    // earlier entries registered waiters: the handler returned 400, then the batch wrote a second
    // response on the connection. Every id must still cancel successfully in the next batch, since
    // an already-cancelled order cannot be cancelled again.
    {
        const auto malformed = slick::sim::integration::httpPost(
            restBase() + "/api/v3/brokerage/orders/batch_cancel",
            json({{"order_ids", json::array({order_ids.front(), 123})}}).dump(),
            auth("alice"));
        EXPECT_EQ(malformed.status, 400u) << malformed.body;
        EXPECT_TRUE(malformed.contains("INVALID_ARGUMENT")) << malformed.body;
    }

    alice.clear_events();
    ASSERT_TRUE(alice.send({{"cmd", "cancel"}, {"order_ids", order_ids}}));
    const auto* cancelAck = alice.wait_for(
        [](const json& event) { return event.value("event", "") == "ack" && event.value("kind", "") == "cancel"; },
        10s);
    ASSERT_NE(cancelAck, nullptr) << dumpEvents(alice.events());
    ASSERT_EQ(cancelAck->at("results").size(), 5u) << cancelAck->dump();
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE((*cancelAck)["results"][i].value("success", false)) << cancelAck->dump();
    }
    EXPECT_EQ((*cancelAck)["results"][4].value("failure_reason", ""), "UNKNOWN_ORDER");
    EXPECT_LT(cancelAck->value("rtt_ms", 10'000.0), 500.0)
        << "batch cancel still polls per order: " << cancelAck->dump();

    // Check the execution-report-backed historical listing through its documented HTTP route.
    const auto listing = slick::sim::integration::httpGet(
        restBase() + "/api/v3/brokerage/orders/historical/batch?product_ids=" + product + "&limit=200",
        auth("alice"));
    ASSERT_EQ(listing.status, 200u) << listing.body;
    const auto body = json::parse(listing.body);
    ASSERT_TRUE(body.contains("orders")) << listing.body;
    ASSERT_EQ(body.value("has_next", true), false) << listing.body;
    EXPECT_GE(body["orders"].size(), 7u) << listing.body;
    // alice's bids were cancelled, and each user only sees their own orders, so the
    // sweep is asserted against carol's listing.
    bool sawCancelledRest = false;
    for (const auto& order : body["orders"]) {
        sawCancelledRest = sawCancelledRest || (order.value("client_order_id", "") == "sol-rest-3" &&
                                                order.value("status", "") == "CANCELLED");
    }
    EXPECT_TRUE(sawCancelledRest) << listing.body;

    const auto carolListing = slick::sim::integration::httpGet(
        restBase() + "/api/v3/brokerage/orders/historical/batch?product_ids=" + product + "&limit=200",
        auth("carol"));
    ASSERT_EQ(carolListing.status, 200u) << carolListing.body;
    const auto carolBody = json::parse(carolListing.body);
    bool sawFilledMarket = false;
    for (const auto& order : carolBody["orders"]) {
        sawFilledMarket = sawFilledMarket || (order.value("client_order_id", "") == "sol-market" &&
                                              order.value("status", "") == "FILLED");
    }
    EXPECT_TRUE(sawFilledMarket) << carolListing.body;
}

TEST(CoinbaseCliTest, TargetsAndLogLevelAreServed) {
    REQUIRE_ENVIRONMENT();
    const auto targets = env().sim().cli(slick::sim::integration::CliCommand::LIST_TARGETS, "");
    EXPECT_TRUE(targets.ok) << targets.text;
    EXPECT_TRUE(targets.contains("sim")) << targets.text;

    const auto current = env().sim().cli(slick::sim::integration::CliCommand::SET_LOG_LEVEL, "sim");
    EXPECT_TRUE(current.ok) << current.text;
    EXPECT_TRUE(current.contains("log level INFO")) << current.text;

    const auto changed = env().sim().cli(slick::sim::integration::CliCommand::SET_LOG_LEVEL, "sim", "debug");
    EXPECT_TRUE(changed.ok) << changed.text;
    EXPECT_TRUE(changed.contains("log level DEBUG")) << changed.text;

    const auto restored = env().sim().cli(slick::sim::integration::CliCommand::SET_LOG_LEVEL, "sim", "info");
    EXPECT_TRUE(restored.ok) << restored.text;
}

