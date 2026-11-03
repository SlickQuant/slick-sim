#include <gtest/gtest.h>
#include <md_feed/multicast_packet_feed.hpp>
#include <nlohmann/json.hpp>
#include <slick/socket/multicast_sender.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using slick::sim::md_feed::MulticastPacketFeed;

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

    feed.set_joined(LINE, false, 0);
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
