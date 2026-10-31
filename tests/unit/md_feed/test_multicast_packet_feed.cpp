#include <gtest/gtest.h>
#include <md_feed/multicast_packet_feed.hpp>
#include <nlohmann/json.hpp>
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
