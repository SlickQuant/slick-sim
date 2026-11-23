// The integration suite (tests/integration/test_*.cpp) runs this Coinbase Advanced Trade client as
// a child process. Each trading user needs a separate process: the SDK reads COINBASE_API_KEY /
// COINBASE_API_SECRET once per process, and the simulator routes reports by the JWT's `sub`.
//
// Commands and events use JSON Lines. stdin carries one command object per line (see
// handleCommand); stdout carries one event object per line, flushed as it happens. SDK logs go to
// stderr to keep them out of the event stream.
//
// This client is test scaffolding. Its throwaway EC key lets the SDK sign a JWT that the simulator
// decodes without verifying (docs/integration-coinbase.md, "Authentication"). It is not intended as
// a sample client.

#include <coinbase/order.hpp>
#include <coinbase/rest.hpp>
#include <coinbase/websocket.hpp>
#include <slick/net/logging.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace {

// Throwaway P-256 key. The simulator does not verify JWT signatures.
constexpr const char* kTestApiSecret =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEICpM7X572b7brE/OLJW/tfVA9Q3PUh425HfdDmL5eHD3oAoGCCqGSM49\n"
    "AwEHoUQDQgAEUn6phNEe3xsop9BS0qlPUvAZiDJen+VrLxmQp2saNgWxERLF+7bK\n"
    "99W+wLoq4o+n8roPobdaMJTdc6MA15qdnA==\n"
    "-----END EC PRIVATE KEY-----\n";

uint64_t nowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

struct Options {
    std::string user;
    std::string product;
    uint16_t rest_port = 0;
    uint16_t ws_port = 0;
    uint16_t md_port = 0;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; ++i) {
        std::string_view flag = argv[i];
        std::string value = argv[i + 1];
        if (flag == "--user") {
            options.user = value;
            ++i;
        } else if (flag == "--product") {
            options.product = value;
            ++i;
        } else if (flag == "--rest-port") {
            options.rest_port = static_cast<uint16_t>(std::stoi(value));
            ++i;
        } else if (flag == "--ws-port") {
            options.ws_port = static_cast<uint16_t>(std::stoi(value));
            ++i;
        } else if (flag == "--md-port") {
            options.md_port = static_cast<uint16_t>(std::stoi(value));
            ++i;
        }
    }
    return options;
}

/// Writes one JSON event per line to stdout. A mutex protects the stream shared by callbacks on the
/// pump thread and commands on the main thread; it is their only shared state.
class EventSink {
public:
    void emit(json event) {
        std::lock_guard lock(mutex_);
        std::cout << event.dump() << '\n' << std::flush;
    }

private:
    std::mutex mutex_;
};

json levelUpdates(const coinbase::Level2UpdateBatch& batch) {
    json updates = json::array();
    for (const auto& update : batch.updates) {
        updates.push_back(json{
            {"side", update.side == coinbase::Side::BUY ? "bid" : "offer"},
            {"price", update.price_level},
            {"qty", update.new_quantity},
            {"event_time", update.event_time},
        });
    }
    return updates;
}

json marketTrades(const std::vector<coinbase::MarketTrade>& trades) {
    json result = json::array();
    for (const auto& trade : trades) {
        result.push_back(json{
            {"trade_id", trade.trade_id},
            {"product_id", trade.product_id},
            {"price", trade.price},
            {"size", trade.size},
            {"side", trade.side == coinbase::Side::BUY ? "BUY" : "SELL"},
            {"time", trade.time},
        });
    }
    return result;
}

json orderJson(const coinbase::Order& order) {
    return json{
        {"order_id", order.order_id},
        {"client_order_id", order.client_order_id},
        {"product_id", order.product_id},
        {"side", order.side == coinbase::Side::BUY ? "BUY" : "SELL"},
        {"type", coinbase::to_string(order.order_type)},
        {"tif", coinbase::to_string(order.time_in_force)},
        {"status", coinbase::to_string(order.status)},
        {"limit_price", order.limit_price},
        {"avg_price", order.avg_price},
        {"cum_qty", order.cumulative_quantity},
        {"leaves_qty", order.leaves_quantity},
        {"completion", order.completion_percentage},
        {"num_fills", order.number_of_fills},
    };
}

/// Tracks when each client order id was submitted, so an execution report can
/// carry its own end-to-end latency. Only touched under the mutex.
class LatencyTable {
public:
    void sent(std::string_view coid) {
        std::lock_guard lock(mutex_);
        sent_[std::string(coid)] = nowNs();
    }

    /// Milliseconds from REST submit to the first terminal user-channel report,
    /// or nullopt if this coid was not submitted by this process.
    std::optional<double> takeTerminal(std::string_view coid) {
        std::lock_guard lock(mutex_);
        auto it = sent_.find(std::string(coid));
        if (it == sent_.end()) {
            return std::nullopt;
        }
        const auto latency = nowNs() - it->second;
        sent_.erase(it);
        return static_cast<double>(latency) / 1e6;
    }

private:
    std::mutex mutex_;
    std::map<std::string, uint64_t> sent_;
};

/// Bridges the SDK's user-thread callbacks to JSON events. Everything here runs
/// on the thread that calls processData(), never concurrently with itself.
class ClientCallbacks : public coinbase::UserThreadWebsocketCallbacks {
public:
    ClientCallbacks(EventSink& sink, LatencyTable& latencies, std::string user)
        : sink_(sink), latencies_(latencies), user_(std::move(user)) {}

    void onMarketDataConnected(coinbase::WebSocketClient*) override {
        market_data_connected_ = true;
        sink_.emit({{"event", "md_connected"}, {"t_ns", nowNs()}});
        announceReady();
    }
    void onUserDataConnected(coinbase::WebSocketClient*) override {
        user_connected_ = true;
        sink_.emit({{"event", "user_connected"}, {"t_ns", nowNs()}});
        announceReady();
    }
    void onMarketDataDisconnected(coinbase::WebSocketClient*) override {
        market_data_connected_ = false;
        sink_.emit({{"event", "md_disconnected"}, {"t_ns", nowNs()}});
    }
    void onUserDataDisconnected(coinbase::WebSocketClient*) override {
        user_connected_ = false;
        sink_.emit({{"event", "user_disconnected"}, {"t_ns", nowNs()}});
    }

    void onLevel2Snapshot(coinbase::WebSocketClient*, uint64_t seq,
                          const coinbase::Level2UpdateBatch& snapshot) override {
        sink_.emit({{"event", "l2"},
                    {"kind", "snapshot"},
                    {"product", snapshot.product_id},
                    {"seq", seq},
                    {"updates", levelUpdates(snapshot)}});
    }
    void onLevel2Updates(coinbase::WebSocketClient*, uint64_t seq,
                         const coinbase::Level2UpdateBatch& updates) override {
        sink_.emit({{"event", "l2"},
                    {"kind", "update"},
                    {"product", updates.product_id},
                    {"seq", seq},
                    {"updates", levelUpdates(updates)}});
    }

    void onMarketTradesSnapshot(coinbase::WebSocketClient*, uint64_t seq,
                                const std::vector<coinbase::MarketTrade>& trades) override {
        sink_.emit({{"event", "trades"},
                    {"kind", "snapshot"},
                    {"seq", seq},
                    {"trades", marketTrades(trades)}});
    }
    void onMarketTrades(coinbase::WebSocketClient*, uint64_t seq,
                        const std::vector<coinbase::MarketTrade>& trades) override {
        sink_.emit({{"event", "trades"},
                    {"kind", "update"},
                    {"seq", seq},
                    {"trades", marketTrades(trades)}});
    }

    void onTickerSnapshot(coinbase::WebSocketClient*, uint64_t seq, uint64_t,
                          const std::vector<coinbase::Ticker>& tickers) override {
        sink_.emit({{"event", "ticker"}, {"kind", "snapshot"}, {"seq", seq}, {"count", tickers.size()}});
    }
    void onTickers(coinbase::WebSocketClient*, uint64_t seq, uint64_t,
                   const std::vector<coinbase::Ticker>& tickers) override {
        sink_.emit({{"event", "ticker"}, {"kind", "update"}, {"seq", seq}, {"count", tickers.size()}});
    }

    void onCandlesSnapshot(coinbase::WebSocketClient*, uint64_t, uint64_t,
                           const std::vector<coinbase::Candle>&) override {}
    void onCandles(coinbase::WebSocketClient*, uint64_t, uint64_t, const std::vector<coinbase::Candle>&) override {}
    void onStatusSnapshot(coinbase::WebSocketClient*, uint64_t, uint64_t,
                          const std::vector<coinbase::Status>&) override {}
    void onStatus(coinbase::WebSocketClient*, uint64_t, uint64_t, const std::vector<coinbase::Status>&) override {}

    void onMarketDataGap(coinbase::WebSocketClient*) override {
        sink_.emit({{"event", "gap"}, {"stream", "market_data"}});
    }
    void onUserDataGap(coinbase::WebSocketClient*) override {
        sink_.emit({{"event", "gap"}, {"stream", "user"}});
    }

    void onUserDataSnapshot(coinbase::WebSocketClient*, uint64_t seq,
                            const std::vector<coinbase::Order>& orders,
                            const std::vector<coinbase::PerpetualFuturePosition>&,
                            const std::vector<coinbase::ExpiringFuturePosition>&) override {
        json snapshot = json::array();
        for (const auto& order : orders) {
            snapshot.push_back(orderJson(order));
        }
        sink_.emit({{"event", "user_snapshot"}, {"seq", seq}, {"orders", snapshot}});
    }

    void onOrderUpdates(coinbase::WebSocketClient*, uint64_t seq,
                        const std::vector<coinbase::Order>& orders) override {
        for (const auto& order : orders) {
            json event = orderJson(order);
            event["event"] = "order";
            event["seq"] = seq;
            event["t_ns"] = nowNs();
            if (order.status == coinbase::OrderStatus::FILLED ||
                order.status == coinbase::OrderStatus::CANCELLED ||
                order.status == coinbase::OrderStatus::EXPIRED ||
                order.status == coinbase::OrderStatus::FAILED) {
                event["terminal"] = true;
                if (auto latency = latencies_.takeTerminal(order.client_order_id)) {
                    event["latency_ms"] = *latency;
                }
            }
            sink_.emit(std::move(event));
        }
    }

    void onMarketDataError(coinbase::WebSocketClient*, std::string&& err) override {
        sink_.emit({{"event", "error"}, {"stream", "market_data"}, {"message", err}});
    }
    void onUserDataError(coinbase::WebSocketClient*, std::string&& err) override {
        sink_.emit({{"event", "error"}, {"stream", "user"}, {"message", err}});
    }

    bool ready() const { return market_data_connected_ && user_connected_; }

private:
    void announceReady() {
        if (ready() && !announced_ready_) {
            announced_ready_ = true;
            sink_.emit({{"event", "ready"}, {"user", user_}, {"t_ns", nowNs()}});
        }
    }

    EventSink& sink_;
    LatencyTable& latencies_;
    std::string user_;
    bool market_data_connected_ = false;
    bool user_connected_ = false;
    bool announced_ready_ = false;
};

coinbase::Side parseSide(std::string_view side) {
    return side == "BUY" ? coinbase::Side::BUY : coinbase::Side::SELL;
}

coinbase::OrderType parseType(std::string_view type) {
    return type == "MARKET" ? coinbase::OrderType::MARKET : coinbase::OrderType::LIMIT;
}

coinbase::TimeInForce parseTif(std::string_view tif) {
    if (tif == "IOC") {
        return coinbase::TimeInForce::IMMEDIATE_OR_CANCEL;
    }
    if (tif == "FOK") {
        return coinbase::TimeInForce::FILL_OR_KILL;
    }
    return coinbase::TimeInForce::GOOD_UNTIL_CANCELLED;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

/// Submits `count` orders over `threads` blocking REST calls and reports the round-trip
/// distribution. The simulator replies with a PENDING_NEW ack, so the measurement covers order
/// entry and excludes the eventual fill.
void runBench(coinbase::CoinbaseRestClient& rest, const json& command, EventSink& sink,
              LatencyTable& latencies) {
    const int count = command.value("count", 100);
    const int threads = std::max(1, command.value("threads", 1));
    const std::string product = command.value("product", std::string("BTC-USD"));
    const auto side = parseSide(command.value("side", std::string("BUY")));
    const auto type = parseType(command.value("type", std::string("LIMIT")));
    const auto tif = parseTif(command.value("tif", std::string("IOC")));
    const double qty = command.value("qty", 0.001);
    const double price = command.value("price", 0.0);
    const std::string prefix = command.value("coid_prefix", std::string("bench"));
    const bool post_only = command.value("post_only", false);

    std::vector<std::vector<double>> rtts(static_cast<size_t>(threads));
    std::vector<int> succeeded(static_cast<size_t>(threads), 0);
    std::vector<int> failed(static_cast<size_t>(threads), 0);

    const auto started = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int worker = 0; worker < threads; ++worker) {
        const int first = count * worker / threads;
        const int last = count * (worker + 1) / threads;
        workers.emplace_back([&, worker, first, last] {
            auto& samples = rtts[static_cast<size_t>(worker)];
            samples.reserve(static_cast<size_t>(last - first));
            for (int i = first; i < last; ++i) {
                const std::string coid = std::format("{}-{}-{}", prefix, worker, i);
                latencies.sent(coid);
                const auto begin = std::chrono::steady_clock::now();
                auto response = rest.create_order(std::string(coid), std::string(product), side, type,
                                                  tif, qty, type == coinbase::OrderType::LIMIT ? price
                                                                                               : std::nan(""),
                                                  post_only);
                const auto end = std::chrono::steady_clock::now();
                samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
                if (response.success) {
                    ++succeeded[static_cast<size_t>(worker)];
                } else {
                    ++failed[static_cast<size_t>(worker)];
                    latencies.takeTerminal(coid);   // no report will come
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::vector<double> all;
    int ok = 0;
    int bad = 0;
    for (size_t i = 0; i < rtts.size(); ++i) {
        all.insert(all.end(), rtts[i].begin(), rtts[i].end());
        ok += succeeded[i];
        bad += failed[i];
    }
    std::sort(all.begin(), all.end());

    sink.emit(json{
        {"event", "bench"},
        {"count", count},
        {"threads", threads},
        {"ok", ok},
        {"failed", bad},
        {"elapsed_ms", elapsed * 1000.0},
        {"orders_per_sec", elapsed > 0 ? static_cast<double>(count) / elapsed : 0.0},
        {"rtt_p50_ms", percentile(all, 0.50)},
        {"rtt_p90_ms", percentile(all, 0.90)},
        {"rtt_p99_ms", percentile(all, 0.99)},
        {"rtt_max_ms", all.empty() ? 0.0 : all.back()},
    });
}

}  // namespace

namespace {

struct ClientContext {
    const Options& options;
    coinbase::CoinbaseRestClient& rest;
    coinbase::WebSocketClient& websocket;
    EventSink& sink;
    LatencyTable& latencies;
    uint64_t next_coid = 0;
};

void handleMdSubscribe(ClientContext& ctx, const json& command) {
    std::vector<std::string> products = command.at("products").get<std::vector<std::string>>();
    std::vector<coinbase::WebSocketChannel> channels;
    for (const auto& channel : command.at("channels")) {
        const auto name = channel.get<std::string>();
        if (name == "level2") {
            channels.push_back(coinbase::WebSocketChannel::LEVEL2);
        } else if (name == "market_trades") {
            channels.push_back(coinbase::WebSocketChannel::MARKET_TRADES);
        } else if (name == "ticker") {
            channels.push_back(coinbase::WebSocketChannel::TICKER);
        } else if (name == "heartbeats") {
            channels.push_back(coinbase::WebSocketChannel::HEARTBEATS);
        }
    }
    ctx.websocket.subscribe(products, channels);
    ctx.sink.emit({{"event", "ack"}, {"kind", "md_subscribe"}, {"products", products}});
}

void handleNew(ClientContext& ctx, const json& command) {
    const std::string product = command.value("product", ctx.options.product);
    std::string coid = command.value("coid", std::string{});
    if (coid.empty()) {
        coid = std::format("{}-{}", ctx.options.user, ++ctx.next_coid);
    }
    const auto side = parseSide(command.value("side", std::string("BUY")));
    const auto type = parseType(command.value("type", std::string("LIMIT")));
    const auto tif = parseTif(command.value("tif", std::string("GTC")));
    const double qty = command.value("qty", 0.0);
    const double price = command.value("price", 0.0);

    ctx.latencies.sent(coid);
    const auto begin = std::chrono::steady_clock::now();
    auto response = ctx.rest.create_order(std::string(coid), std::string(product), side, type, tif, qty,
                                          type == coinbase::OrderType::LIMIT ? price : std::nan(""),
                                          command.value("post_only", false));
    const auto rtt =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

    json event{{"event", "ack"},
               {"kind", "new"},
               {"client_order_id", coid},
               {"success", response.success},
               {"rtt_ms", rtt}};
    if (response.success) {
        event["order_id"] = response.success_response.order_id;
        event["product_id"] = response.success_response.product_id;
    } else {
        event["error"] = response.error_response.message;
        ctx.latencies.takeTerminal(coid);   // nothing will report
    }
    ctx.sink.emit(std::move(event));
}

void handleCancel(ClientContext& ctx, const json& command) {
    std::vector<std::string> ids = command.at("order_ids").get<std::vector<std::string>>();
    std::vector<std::string_view> views;
    views.reserve(ids.size());
    for (const auto& id : ids) {
        views.push_back(id);
    }
    const auto begin = std::chrono::steady_clock::now();
    auto results = ctx.rest.cancel_orders(views);
    const auto rtt =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    json entries = json::array();
    for (const auto& result : results) {
        entries.push_back(json{{"success", result.success},
                               {"failure_reason", result.failure_reason},
                               {"order_id", result.order_id}});
    }
    ctx.sink.emit({{"event", "ack"}, {"kind", "cancel"}, {"results", entries}, {"rtt_ms", rtt}});
}

}  // namespace


namespace {

void handleEdit(ClientContext& ctx, const json& command) {
    const auto begin = std::chrono::steady_clock::now();
    auto response = ctx.rest.modify_order(command.at("order_id").get<std::string>(),
                                          command.value("product", ctx.options.product),
                                          command.value("price", 0.0), command.value("size", 0.0));
    const auto rtt =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    ctx.sink.emit({{"event", "ack"},
                   {"kind", "edit"},
                   {"order_id", command.at("order_id")},
                   {"success", response.success},
                   {"errors", response.errors},
                   {"rtt_ms", rtt}});
}

void handleOrders(ClientContext& ctx, const json& command) {
    coinbase::OrderQueryParams query;
    query.limit = static_cast<uint32_t>(command.value("limit", 100));
    if (command.contains("product")) {
        query.product_ids = std::vector<std::string>{command.value("product", ctx.options.product)};
    }
    if (command.contains("status")) {
        query.order_status = std::vector<coinbase::OrderStatus>{
            coinbase::to_order_status(command.value("status", std::string("OPEN")))};
    }
    auto orders = ctx.rest.list_orders(query);
    json entries = json::array();
    for (const auto& order : orders) {
        entries.push_back(json{{"order_id", order.order_id},
                               {"client_order_id", order.client_order_id},
                               {"status", coinbase::to_string(order.status)},
                               {"leaves_qty", order.leaves_quantity}});
    }
    ctx.sink.emit({{"event", "ack"}, {"kind", "orders"}, {"count", orders.size()}, {"orders", entries}});
}

/// Returns false when the client should stop.
bool handleCommand(ClientContext& ctx, const json& command) {
    const std::string cmd = command.value("cmd", std::string{});
    if (cmd == "quit") {
        return false;
    }
    if (cmd == "md_subscribe") {
        handleMdSubscribe(ctx, command);
    } else if (cmd == "new") {
        handleNew(ctx, command);
    } else if (cmd == "cancel") {
        handleCancel(ctx, command);
    } else if (cmd == "edit") {
        handleEdit(ctx, command);
    } else if (cmd == "orders") {
        handleOrders(ctx, command);
    } else if (cmd == "bench") {
        runBench(ctx.rest, command, ctx.sink, ctx.latencies);
    } else {
        ctx.sink.emit({{"event", "error"}, {"message", std::format("unknown command '{}'", cmd)}});
    }
    return true;
}

}  // namespace


int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.user.empty() || options.rest_port == 0 || options.ws_port == 0 || options.md_port == 0) {
        std::cerr << "usage: coinbase_client --user U --rest-port P --ws-port P --md-port P [--product BTC-USD]\n";
        return 2;
    }

    // The SDK signs with these on the first request and caches them for the
    // lifetime of the process. The sub's last path segment is the user id the
    // simulator routes execution reports by.
    setEnv("COINBASE_API_KEY", "organizations/integration-test/actors/" + options.user);
    setEnv("COINBASE_API_SECRET", kTestApiSecret);

    slick::net::set_log_handler(
        [](slick::net::LogLevel level, const char* format_text, std::format_args args) {
            if (level >= slick::net::LogLevel::Warn) {
                std::cerr << std::vformat(format_text, args) << '\n';
            }
        },
        []() { return slick::net::LogLevel::Warn; });

    EventSink sink;
    LatencyTable latencies;
#if defined(_WIN32)
    const auto pid = _getpid();
#else
    const auto pid = getpid();
#endif
    sink.emit({{"event", "hello"}, {"user", options.user}, {"pid", pid}});

    // The parent starts clients only after the simulator answers on this port, so
    // the product cache this constructor fetches is populated on the first try.
    coinbase::CoinbaseRestClient rest(std::format("http://127.0.0.1:{}", options.rest_port));

    ClientCallbacks callbacks(sink, latencies, options.user);
    std::atomic_bool running{true};
    std::thread pump([&callbacks, &running] {
        while (running.load(std::memory_order_relaxed)) {
            callbacks.processData(200);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto websocket = std::make_unique<coinbase::WebSocketClient>(
        &callbacks,
        std::format("ws://127.0.0.1:{}/", options.md_port),
        std::format("ws://127.0.0.1:{}/", options.ws_port),
        /*mux_shm_name=*/nullptr,
        /*md_read_buffer_size=*/1u << 20, /*md_record_size=*/1u << 16, /*md_shm=*/nullptr,
        /*user_read_buffer_size=*/1u << 20, /*user_record_size=*/1u << 16, /*user_shm=*/nullptr,
        /*write_buffer_size=*/1u << 20);
    websocket->subscribe({}, {coinbase::WebSocketChannel::USER, coinbase::WebSocketChannel::HEARTBEATS});

    ClientContext ctx{options, rest, *websocket, sink, latencies};

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        json command;
        try {
            command = json::parse(line);
        } catch (const std::exception& ex) {
            sink.emit({{"event", "error"}, {"message", std::format("invalid command JSON: {}", ex.what())}});
            continue;
        }
        try {
            if (!handleCommand(ctx, command)) {
                break;
            }
        } catch (const std::exception& ex) {
            sink.emit({{"event", "error"}, {"message", ex.what()}});
        }
    }

    running.store(false, std::memory_order_relaxed);
    pump.join();
    // Stop processData() before destroying the client. This thread can safely access the callbacks'
    // routing tables once the pump has stopped.
    websocket.reset();
    sink.emit({{"event", "bye"}});
    return 0;
}

