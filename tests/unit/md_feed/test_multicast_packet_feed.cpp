#include <gtest/gtest.h>
#include <md_feed/multicast_packet_feed.hpp>
#include <nlohmann/json.hpp>
#include <slick/socket/multicast_sender.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <csignal>
#include <ctime>
#include <pthread.h>
#include <sched.h>
#endif

namespace slick::sim::md_feed {

/// The feed's friend, so a test can reach the receiver thread to stall it.
struct MulticastPacketFeedTestAccess {
    static std::thread &receiver(MulticastPacketFeed &feed) { return feed.thread_; }
    static bool left(const MulticastPacketFeed &feed, uint8_t line) { return feed.left_ & (1u << line); }
    /// True once the receiver thread has answered the change handed to it for `line`.
    static bool answered(const MulticastPacketFeed &feed, uint8_t line) {
        return feed.request_[line].load() >= MulticastPacketFeed::REQUEST_DONE;
    }
};

}   // namespace slick::sim::md_feed

using slick::sim::md_feed::MulticastPacketFeed;
using slick::sim::md_feed::MulticastPacketFeedTestAccess;

namespace {

struct Received {
    uint8_t line;
    std::vector<uint8_t> bytes;
};

std::vector<Received> drainAll(MulticastPacketFeed &feed) {
    std::vector<Received> out;
    while (feed.drain([&](uint8_t line, const uint8_t *data, size_t size) {
        out.push_back({line, std::vector<uint8_t>(data, data + size)});
    }) > 0) {
    }
    return out;
}

/// Asks for `line` to be left and waits until the receiver thread has done it, so nothing sent
/// afterwards can be read by a socket that is still joined. False if it never was, in two seconds.
bool leaveAndWait(MulticastPacketFeed &feed, uint8_t line) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!MulticastPacketFeedTestAccess::left(feed, line)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        feed.set_joined(line, false, 0);   // hands it over, then collects the answer
        std::this_thread::yield();
    }
    return true;
}

bool hasTag(const std::vector<Received> &got, uint8_t tag) {
    return std::any_of(got.begin(), got.end(), [&](const Received &r) { return !r.bytes.empty() && r.bytes[0] == tag; });
}

/// Sends `tag` until the feed hands one over, for up to two seconds.
bool sendUntilReceived(slick::socket::MulticastSender &sender, MulticastPacketFeed &feed, uint8_t tag) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        sender.send_data(&tag, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (hasTag(drainAll(feed), tag)) {
            return true;
        }
    }
    return false;
}

}   // namespace

TEST(MulticastPacketFeedTest, PacketsReachTheDrainInOrderWithTheirLine) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096}});
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {9};
    feed.deliver(0, a, sizeof(a));
    feed.deliver(2, b, sizeof(b));

    const auto got = drainAll(feed);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].line, 0);
    EXPECT_EQ(got[0].bytes, (std::vector<uint8_t>{1, 2, 3}));
    EXPECT_EQ(got[1].line, 2);
    EXPECT_EQ(got[1].bytes, (std::vector<uint8_t>{9}));
}

TEST(MulticastPacketFeedTest, OnePollHandsOverABoundedBatch) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096}, {"max_packets_per_poll", 2}});
    const uint8_t p[] = {7};
    for (int i = 0; i < 5; ++i) {
        feed.deliver(0, p, sizeof(p));
    }
    size_t seen = 0;
    const auto count = [&](uint8_t, const uint8_t *, size_t) { ++seen; };
    EXPECT_EQ(feed.drain(count), 2u);
    EXPECT_EQ(feed.drain(count), 2u);
    EXPECT_EQ(feed.drain(count), 1u);
    EXPECT_EQ(seen, 5u);
}

TEST(MulticastPacketFeedTest, AMutedLineIsDroppedOnArrival) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096}});
    const uint8_t p[] = {7};
    feed.mute(1, true);
    feed.deliver(1, p, sizeof(p));
    feed.deliver(0, p, sizeof(p));
    feed.mute(1, false);
    feed.deliver(1, p, sizeof(p));

    const auto got = drainAll(feed);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].line, 0);
    EXPECT_EQ(got[1].line, 1);
}

// A left line is muted as well, so what was already in the socket buffer is dropped.
TEST(MulticastPacketFeedTest, ALeftLineIsMutedUntilRejoined) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096}});
    const uint8_t p[] = {7};
    feed.set_joined(1, false, 0);
    EXPECT_FALSE(feed.deliver(1, p, sizeof(p)));
    EXPECT_TRUE(feed.deliver(0, p, sizeof(p)));
    feed.set_joined(1, false, 0);
    EXPECT_FALSE(feed.deliver(1, p, sizeof(p)));
    feed.set_joined(1, true, 0);
    EXPECT_TRUE(feed.deliver(1, p, sizeof(p)));
}

namespace {

/// Records every membership change asked of the socket, and fails the next
/// `failures` of them.
class ScriptedFeed : public MulticastPacketFeed {
public:
    struct Call {
        uint8_t line;
        bool join;
    };

    ScriptedFeed() : MulticastPacketFeed("test", nlohmann::json{{"queue_size", 4096}}) {}

    std::vector<Call> calls;
    int failures = 0;

protected:
    int change_membership(uint8_t line, bool join) override {
        calls.push_back({line, join});
        if (failures > 0) {
            --failures;
            return 10065;   // WSAEHOSTUNREACH; any non-zero error will do
        }
        return 0;
    }
};

constexpr uint64_t RETRY = MulticastPacketFeed::MEMBERSHIP_RETRY_NS;
const uint8_t PACKET[] = {7};

}   // namespace

// A rejoin that fails must not count as done, or recovery waits for ever on a group
// the socket never rejoined. It is tried again, though no sooner than the backoff.
TEST(MulticastPacketFeedTest, AFailedRejoinIsRetriedUntilItSucceeds) {
    ScriptedFeed feed;
    feed.set_joined(1, false, 0);
    ASSERT_EQ(feed.calls.size(), 1u);

    feed.failures = 2;
    feed.set_joined(1, true, 1000);
    feed.set_joined(1, true, 1000 + RETRY - 1);
    EXPECT_EQ(feed.calls.size(), 2u) << "retried before the backoff";
    EXPECT_TRUE(feed.deliver(1, PACKET, sizeof(PACKET))) << "a line wanted back is unmuted while its rejoin retries";

    feed.set_joined(1, true, 1000 + RETRY);       // fails again
    feed.set_joined(1, true, 1000 + 2 * RETRY);   // succeeds
    feed.set_joined(1, true, 1000 + 3 * RETRY);   // nothing left to do
    ASSERT_EQ(feed.calls.size(), 4u);
    for (size_t i = 1; i < feed.calls.size(); ++i) {
        EXPECT_EQ(feed.calls[i].line, 1);
        EXPECT_TRUE(feed.calls[i].join);
    }
}

TEST(MulticastPacketFeedTest, AFailedLeaveIsRetriedAndTheLineStaysMuted) {
    ScriptedFeed feed;
    feed.failures = 1;
    feed.set_joined(1, false, 0);
    EXPECT_FALSE(feed.deliver(1, PACKET, sizeof(PACKET)));
    feed.set_joined(1, false, RETRY);
    feed.set_joined(1, false, 2 * RETRY);
    ASSERT_EQ(feed.calls.size(), 2u);
    EXPECT_FALSE(feed.calls[1].join);
}

// A failed leave left the socket joined, so wanting the line back needs no call, and
// wanting it left again is a fresh attempt that does not wait out the old backoff.
TEST(MulticastPacketFeedTest, ChangingTheWantedStateDropsAFailedAttempt) {
    ScriptedFeed feed;
    feed.failures = 1;
    feed.set_joined(1, false, 0);
    feed.set_joined(1, true, 1);
    EXPECT_EQ(feed.calls.size(), 1u);
    EXPECT_TRUE(feed.deliver(1, PACKET, sizeof(PACKET)));
    feed.set_joined(1, false, 2);
    EXPECT_EQ(feed.calls.size(), 2u);
}

// start() joins every group, so a line the venue still wants left stays muted and is
// left again on its next set_joined(), not taken as left already.
TEST(MulticastPacketFeedTest, StartingRejoinsEveryGroup) {
    ScriptedFeed feed;
    feed.set_joined(1, false, 0);
    feed.start();
    EXPECT_FALSE(feed.deliver(1, PACKET, sizeof(PACKET)));
    feed.set_joined(1, false, 0);
    EXPECT_EQ(feed.calls.size(), 2u);
    feed.stop();
}

// Leaving must drop the group at the socket, not only mute it, so the network stops
// sending a snapshot loop nobody reads. The mute is lifted after leaving to see past
// it. Loopback multicast on this host only (TTL 0); skipped where there is none.
TEST(MulticastPacketFeedTest, ALeftGroupStopsReachingTheSocket) {
    constexpr const char *GROUP = "239.255.42.7";
    constexpr uint16_t PORT = 19442;
    constexpr uint8_t LINE = 1;
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 1 << 16},
                                                    {"snapshot", {{"address", GROUP}, {"port", PORT}}}});
    ASSERT_TRUE(feed.add_line("snapshot", LINE));
    feed.start();

    slick::socket::MulticastSenderConfig config;
    config.multicast_address = GROUP;
    config.port = PORT;
    config.ttl = 0;
    config.enable_loopback = true;
    slick::socket::MulticastSender sender("test sender", config);
    if (!sender.start() || !sendUntilReceived(sender, feed, 1)) {
        feed.stop();
        GTEST_SKIP() << "no loopback multicast on this host";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    drainAll(feed);   // stragglers of the joined phase

    ASSERT_TRUE(leaveAndWait(feed, LINE)) << "the receiver thread never left the group";
    feed.mute(LINE, false);
    const uint8_t left = 2;
    for (int i = 0; i < 5; ++i) {
        sender.send_data(&left, 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(hasTag(drainAll(feed), left)) << "the socket still receives a group it left";

    feed.set_joined(LINE, true, 0);
    EXPECT_TRUE(sendUntilReceived(sender, feed, 3)) << "the rejoined group never arrived";

    sender.stop();
    feed.stop();
}

TEST(MulticastPacketFeedTest, LinesAreAddedOnlyForConfiguredGroups) {
    MulticastPacketFeed feed("test", nlohmann::json{{"live", {{"address", "239.255.1.1"}, {"port", 30001}}}});
    EXPECT_TRUE(feed.has_group("live"));
    EXPECT_FALSE(feed.has_group("snapshot"));
    EXPECT_TRUE(feed.add_line("live", 0));
    EXPECT_FALSE(feed.add_line("snapshot", 1));
}

// reserve() throws above the queue's size and deliver() runs on a receiver thread,
// where the exception would be std::terminate, so the packet is refused here instead.
// Every packet that size is refused, so they are counted and only the first is logged.
TEST(MulticastPacketFeedTest, APacketBiggerThanTheQueueIsRefused) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 16}});
    const std::vector<uint8_t> big(64, 0xAB);
    EXPECT_FALSE(feed.deliver(0, big.data(), big.size()));
    EXPECT_FALSE(feed.deliver(0, big.data(), big.size()));
    EXPECT_EQ(feed.oversized_count(), 2u);
    EXPECT_TRUE(drainAll(feed).empty());

    const uint8_t small[] = {1};
    EXPECT_TRUE(feed.deliver(0, small, sizeof(small)));
    EXPECT_EQ(drainAll(feed).size(), 1u);
    EXPECT_EQ(feed.oversized_count(), 2u);
}

// The queue overwrites what the exchange thread has not drained yet; the counter is
// what turns that silent loss into something a feed can report. It counts bytes, so
// what was drained and what was lost add up to everything delivered.
TEST(MulticastPacketFeedTest, OverwrittenBytesAreCounted) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096}});
    const uint8_t p[] = {7, 7, 7};
    constexpr uint64_t entry = sizeof(p) + 1;   // the line byte, then the packet
    // Four bytes an entry against a 4 KiB ring: far more than it can hold undrained.
    for (int i = 0; i < 4000; ++i) {
        ASSERT_TRUE(feed.deliver(0, p, sizeof(p)));
    }
    const auto drained = drainAll(feed);
    EXPECT_GT(drained.size(), 0u);
    EXPECT_LT(drained.size(), 4000u);   // some were overwritten, not all delivered
    EXPECT_EQ(drained.size() * entry + feed.loss_count(), 4000u * entry);
}

// A poll budget of 0 would let drain() hand over nothing, for ever. It comes from config,
// so it is refused in every build, not only where asserts run.
TEST(MulticastPacketFeedTest, AZeroPollBudgetIsRefused) {
    EXPECT_THROW(MulticastPacketFeed("test", nlohmann::json{{"queue_size", 4096}, {"max_packets_per_poll", 0}}),
                 std::runtime_error);
}

namespace {

// Receives on `config`'s "snapshot" group as line 1, then leaves and rejoins it. A left group
// must stop reaching the socket, which is what tells a membership change that was made from one
// that was only reported; skipped where this host has no loopback multicast.
void expectReceivesLeavesAndRejoins(nlohmann::json config, const char *group, uint16_t port) {
    config["queue_size"] = 1 << 16;
    config["snapshot"] = {{"address", group}, {"port", port}};
    MulticastPacketFeed feed("test", config);
    ASSERT_TRUE(feed.add_line("snapshot", 1));
    feed.start();

    slick::socket::MulticastSenderConfig sender_config;
    sender_config.multicast_address = group;
    sender_config.port = port;
    sender_config.ttl = 0;
    sender_config.enable_loopback = true;
    slick::socket::MulticastSender sender("test sender", sender_config);
    if (!sender.start() || !sendUntilReceived(sender, feed, 1)) {
        feed.stop();
        GTEST_SKIP() << "no loopback multicast on this host";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    drainAll(feed);   // stragglers of the joined phase

    ASSERT_TRUE(leaveAndWait(feed, 1)) << "the receiver thread never left the group";
    feed.mute(1, false);
    const uint8_t left = 2;
    for (int i = 0; i < 5; ++i) {
        sender.send_data(&left, 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(hasTag(drainAll(feed), left)) << "the socket still receives a group it left";

    feed.set_joined(1, true, 0);
    EXPECT_TRUE(sendUntilReceived(sender, feed, 3)) << "the rejoined group never arrived";
    sender.stop();
    feed.stop();
}

}   // namespace

#if defined(__linux__)
// The receiver thread is pinned to the core asked for, and still delivers and changes membership.
// The core is one this process may use, so the pin is made on any runner, and the thread's own
// affinity is read back, so a pin that fell through to the unpinned path fails the test.
TEST(MulticastPacketFeedTest, APinnedReceiverIsPinnedAndStillReceivesAndChangesMembership) {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    ASSERT_EQ(sched_getaffinity(0, sizeof(allowed), &allowed), 0);
    int cpu = -1;
    for (int i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &allowed)) {
            cpu = i;
        }
    }
    ASSERT_GE(cpu, 0);

    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 1 << 16},
                                                    {"receiver_cpu", cpu},
                                                    {"receive_buffer_size", 1 << 26},
                                                    {"snapshot", {{"address", "239.255.42.8"}, {"port", 19443}}}});
    ASSERT_TRUE(feed.add_line("snapshot", 1));
    feed.start();
    bool pinned = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!pinned && std::chrono::steady_clock::now() < deadline) {   // the thread pins itself as it starts
        cpu_set_t actual;
        CPU_ZERO(&actual);
        ASSERT_EQ(pthread_getaffinity_np(MulticastPacketFeedTestAccess::receiver(feed).native_handle(),
                                         sizeof(actual), &actual), 0);
        pinned = CPU_COUNT(&actual) == 1 && CPU_ISSET(cpu, &actual);
        if (!pinned) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    feed.stop();
    EXPECT_TRUE(pinned) << "the receiver thread is not pinned to core " << cpu;

    expectReceivesLeavesAndRejoins(nlohmann::json{{"receiver_cpu", cpu}, {"receive_buffer_size", 1 << 26}},
                                   "239.255.42.8", 19443);
}
#endif

// A pin that cannot be made (no such core) must leave a working receiver, which sleeps between
// sweeps instead of spinning on a core it does not own.
TEST(MulticastPacketFeedTest, AnImpossiblePinStillReceivesAndChangesMembership) {
    expectReceivesLeavesAndRejoins(nlohmann::json{{"receiver_cpu", 1 << 20}}, "239.255.42.11", 19446);
}

// The receiver thread reads receiver_cpu before it runs; a value that is not a number would
// otherwise throw there, where it is std::terminate.
TEST(MulticastPacketFeedTest, ANonNumericReceiverCpuIsRefused) {
    EXPECT_THROW(MulticastPacketFeed("test", nlohmann::json{{"queue_size", 4096}, {"receiver_cpu", "3"}}),
                 nlohmann::json::exception);
}

// A second start() while the thread runs would destroy a joinable std::thread; slick-socket's
// own receivers only warn, so this does too. The feed can be restarted after stop().
TEST(MulticastPacketFeedTest, StartingTwiceKeepsTheOneReceiverThread) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096},
                                                    {"snapshot", {{"address", "239.255.42.10"}, {"port", 19445}}}});
    ASSERT_TRUE(feed.add_line("snapshot", 1));
    feed.start();
    feed.start();
    feed.stop();
    feed.start();
    feed.stop();
}

// A line cannot be added under the running receiver thread.
TEST(MulticastPacketFeedTest, ALineIsNotAddedWhileStarted) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096},
                                                    {"snapshot", {{"address", "239.255.42.12"}, {"port", 19447}}},
                                                    {"live", {{"address", "239.255.42.13"}, {"port", 19448}}}});
    ASSERT_TRUE(feed.add_line("snapshot", 1));
    feed.start();
    EXPECT_FALSE(feed.add_line("live", 0));
    feed.stop();
    EXPECT_TRUE(feed.add_line("live", 0));
}

#if defined(__linux__)

namespace {

std::atomic<bool> g_stalled{false};

void stallThisThread(int) {
    g_stalled.store(true);
    const timespec stall{0, 350'000'000};
    nanosleep(&stall, nullptr);
}

}   // namespace

// A receiver thread too stalled to make a leave must neither hold up the exchange thread nor make
// the leave after the venue has asked for the group back: it would be made when the thread woke,
// and the feed would think it joined and unmuted, on a socket that had left.
TEST(MulticastPacketFeedTest, AMembershipChangeThatTimedOutIsNotMadeLater) {
    constexpr const char *GROUP = "239.255.42.9";
    constexpr uint16_t PORT = 19444;
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 1 << 16},
                                                    {"snapshot", {{"address", GROUP}, {"port", PORT}}}});
    ASSERT_TRUE(feed.add_line("snapshot", 1));
    // The receiver thread inherits this thread's signal mask, and a runner may have blocked
    // SIGUSR2, which would keep the stall signal from ever reaching it.
    sigset_t unblock, saved;
    sigemptyset(&unblock);
    sigaddset(&unblock, SIGUSR2);
    ASSERT_EQ(pthread_sigmask(SIG_UNBLOCK, &unblock, &saved), 0);
    feed.start();
    pthread_sigmask(SIG_SETMASK, &saved, nullptr);

    slick::socket::MulticastSenderConfig config;
    config.multicast_address = GROUP;
    config.port = PORT;
    config.ttl = 0;
    config.enable_loopback = true;
    slick::socket::MulticastSender sender("test sender", config);
    if (!sender.start() || !sendUntilReceived(sender, feed, 1)) {
        feed.stop();
        GTEST_SKIP() << "no loopback multicast on this host";
    }

    struct sigaction stall{};
    stall.sa_handler = stallThisThread;
    sigemptyset(&stall.sa_mask);
    struct sigaction previous{};
    ASSERT_EQ(sigaction(SIGUSR2, &stall, &previous), 0);
    g_stalled.store(false);
    ASSERT_EQ(pthread_kill(MulticastPacketFeedTestAccess::receiver(feed).native_handle(), SIGUSR2), 0);
    const auto handler_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!g_stalled.load() && std::chrono::steady_clock::now() < handler_deadline) {   // until the thread is in the handler
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!g_stalled.load()) {
        sender.stop();
        feed.stop();   // joins the receiver: no signal can arrive after the handler goes
        sigaction(SIGUSR2, &previous, nullptr);
        FAIL() << "the receiver thread never ran the stall handler";
    }

    constexpr uint64_t NOW = 1'000'000'000;
    const auto before = std::chrono::steady_clock::now();
    feed.set_joined(1, false, NOW);       // handed over, not taken up: returns at once
    feed.set_joined(1, true, NOW + 1);    // and now the venue wants the group: withdrawn
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::milliseconds(100))
        << "set_joined waited for a stalled receiver thread";
    EXPECT_TRUE(sendUntilReceived(sender, feed, 2)) << "a leave that was reported failed was made anyway";

    sender.stop();
    feed.stop();   // joins the receiver: no signal can arrive after the handler goes
    sigaction(SIGUSR2, &previous, nullptr);
}

#endif   // __linux__

// A rejoin asked for while a leave is in flight collects the leave's answer first. That leave did
// leave the socket, so it must be recorded then: were the rejoin to fail, a feed that still took
// the socket for joined would never retry it.
TEST(MulticastPacketFeedTest, ALeaveCollectedBeforeARejoinIsRecorded) {
    MulticastPacketFeed feed("test", nlohmann::json{{"queue_size", 4096},
                                                    {"snapshot", {{"address", "239.255.42.14"}, {"port", 19449}}}});
    ASSERT_TRUE(feed.add_line("snapshot", 1));
    feed.start();
    feed.set_joined(1, false, 0);   // handed over
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!MulticastPacketFeedTestAccess::answered(feed, 1) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(MulticastPacketFeedTestAccess::answered(feed, 1));
    feed.set_joined(1, true, 0);    // collects the leave's answer, hands over the rejoin
    EXPECT_TRUE(MulticastPacketFeedTestAccess::left(feed, 1)) << "a leave that was made was not recorded";
    feed.stop();
}
