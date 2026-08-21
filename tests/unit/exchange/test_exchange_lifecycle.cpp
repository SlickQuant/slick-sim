#include <gtest/gtest.h>
#include <exchange/exchange.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <future>

using namespace slick::sim;
using namespace slick::sim::exch;

namespace {

/// The base Exchange constructor only requires these two keys to be present; it
/// does not build a gateway or a publisher from them. Queue sizes are shrunk from
/// the 1M/16M defaults so the fixture does not allocate hundreds of megabytes per
/// test.
nlohmann::json minimalConfig() {
    return nlohmann::json{
        {"request_queue_size", 1024},
        {"response_queue_size", 1024},
        {"md_queue_size", 4096},
        {"order_gateway", nlohmann::json::object()},
        {"md_publisher", nlohmann::json::object()},
    };
}

/// Runs `fn` on a helper thread and fails rather than blocking the suite forever
/// if it does not return. Every assertion here is about shutdown terminating.
template <typename F>
[[nodiscard]] bool completesWithin(std::chrono::milliseconds limit, F &&fn) {
    auto fut = std::async(std::launch::async, std::forward<F>(fn));
    return fut.wait_for(limit) == std::future_status::ready;
}

/// Counts poll() calls so a test can prove the base class actually loops.
class CountingExchange : public Exchange {
public:
    explicit CountingExchange(const nlohmann::json &config)
        : Exchange(Venue::STOCK, config) {}

    std::atomic_uint_fast64_t polls{0};

protected:
    void poll() override { polls.fetch_add(1, std::memory_order_relaxed); }
};

}   // namespace

// The base class used to call processRequest() exactly once with no loop, so any
// adapter that did not override start() bound its ports and then went silent.
// The loop lives in Exchange::start() now and adapters override poll() instead.
TEST(ExchangeLifecycleTest, BaseStartLoopsUntilStopped) {
    CountingExchange exch(minimalConfig());
    exch.start();

    // Busy-wait briefly rather than sleeping a fixed interval: the point is that
    // poll() is called repeatedly, not how fast.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (exch.polls.load(std::memory_order_relaxed) < 100
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    EXPECT_GE(exch.polls.load(std::memory_order_relaxed), 100u)
        << "Exchange::start() is not looping - poll() ran "
        << exch.polls.load(std::memory_order_relaxed) << " time(s)";

    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));
}

// main() stops every exchange explicitly and then ~Exchange() stops it again.
// That was harmless while stop() only touched individually-guarded members, but
// it now stops the market-data feeds too, and CoinbaseLiveWSFeed::stop()
// unsubscribes upstream - so a second pass must be a no-op, not a second
// unsubscribe.
TEST(ExchangeLifecycleTest, StopIsIdempotent) {
    CountingExchange exch(minimalConfig());
    exch.start();

    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));
    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));
    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));
}

// The destructor calls stop(), so destroying a still-running exchange must also
// terminate - this is the path every unit test that builds an adapter takes.
TEST(ExchangeLifecycleTest, DestructorStopsARunningExchange) {
    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [] {
        CountingExchange exch(minimalConfig());
        exch.start();
    }));
}

// stop() before start() must not hang or trip the run-once guard in a way that
// leaves a later start()/stop() pair broken.
TEST(ExchangeLifecycleTest, StopBeforeStartThenStartStop) {
    CountingExchange exch(minimalConfig());

    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));

    exch.start();
    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));
}

// A disabled exchange never starts a thread; stopping it must still return.
TEST(ExchangeLifecycleTest, DisabledExchangeStartsNothingAndStopsCleanly) {
    auto config = minimalConfig();
    config["enabled"] = false;

    CountingExchange exch(config);
    exch.start();

    EXPECT_EQ(exch.polls.load(std::memory_order_relaxed), 0u);
    EXPECT_TRUE(completesWithin(std::chrono::seconds(5), [&] { exch.stop(); }));
}
