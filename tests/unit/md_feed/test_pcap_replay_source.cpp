#include <gtest/gtest.h>
#include <md_feed/pcap_replay_source.hpp>

#include "pcap_test_util.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

using slick::sim::md_feed::PcapLine;
using slick::sim::md_feed::PcapReplaySource;
using slick::sim::md_feed::ReplayPacer;
using namespace slick::sim::md_feed::test;

namespace {

constexpr uint64_t SEC = 1'000'000'000ull;

struct Got {
    uint64_t ts_ns;
    uint8_t line;
    uint8_t tag;
};

/// Drains a source to its end, waiting up to five seconds for the reader.
std::vector<Got> drainToEnd(PcapReplaySource &source) {
    std::vector<Got> out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        PcapReplaySource::Item item;
        if (source.peek(item)) {
            out.push_back({item.ts_ns, item.line, item.data[0]});
            source.pop();
        } else if (source.finished()) {
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return out;
}

TestFrame frame(uint64_t ts_ns, const char *addr, uint16_t port, uint8_t tag) {
    return {ts_ns, addr, port, {tag}};
}

}   // namespace

TEST(PcapReplaySourceTest, MergesLinesByCaptureTime) {
    TempDir dir;
    write_pcap(dir.path() + "/inc.pcap", {frame(10 * SEC, "224.0.31.1", 14310, 1), frame(30 * SEC, "224.0.31.1", 14310, 3)});
    write_pcap(dir.path() + "/snap.pcap", {frame(20 * SEC, "224.0.32.3", 15312, 2), frame(40 * SEC, "224.0.32.3", 15312, 4)});
    PcapReplaySource source("t", "", 0, 100 * SEC,
                            {{0, "224.0.31.1", 14310, {dir.path() + "/inc.pcap"}},
                             {2, "224.0.32.3", 15312, {dir.path() + "/snap.pcap"}}});
    source.start();
    const auto got = drainToEnd(source);
    ASSERT_EQ(got.size(), 4u);
    EXPECT_EQ(got[0].tag, 1);
    EXPECT_EQ(got[1].tag, 2);
    EXPECT_EQ(got[1].line, 2);
    EXPECT_EQ(got[2].tag, 3);
    EXPECT_EQ(got[3].tag, 4);
    EXPECT_TRUE(source.error().empty());
}

TEST(PcapReplaySourceTest, EqualTimestampsKeepLineOrder) {
    TempDir dir;
    write_pcap(dir.path() + "/a.pcap", {frame(10 * SEC, "224.0.31.1", 14310, 1)});
    write_pcap(dir.path() + "/b.pcap", {frame(10 * SEC, "224.0.31.22", 14310, 2)});
    PcapReplaySource source("t", "", 0, 100 * SEC,
                            {{0, "224.0.31.1", 14310, {dir.path() + "/a.pcap"}},
                             {1, "224.0.31.22", 14310, {dir.path() + "/b.pcap"}}});
    source.start();
    const auto got = drainToEnd(source);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].line, 0);
    EXPECT_EQ(got[1].line, 1);
}

TEST(PcapReplaySourceTest, OnlyTheWindowIsReplayedAndOtherGroupsAreIgnored) {
    TempDir dir;
    write_pcap(dir.path() + "/a.pcap",
               {frame(5 * SEC, "224.0.31.1", 14310, 1), frame(10 * SEC, "224.0.31.1", 14310, 2),
                frame(12 * SEC, "224.0.99.9", 1, 9),  // another group in the same file
                frame(20 * SEC, "224.0.31.1", 14310, 3), frame(25 * SEC, "224.0.31.1", 14310, 4)});
    PcapReplaySource source("t", "", 10 * SEC, 20 * SEC, {{0, "224.0.31.1", 14310, {dir.path() + "/a.pcap"}}});
    source.start();
    const auto got = drainToEnd(source);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].tag, 2);
    EXPECT_EQ(got[1].tag, 3);
}

TEST(PcapReplaySourceTest, ResolvesDailyFilesAndDropsTheOverlapBetweenThem) {
    TempDir dir;
    // 2026-11-06 00:00:00 UTC.
    const uint64_t day = 1'793'923'200ull;
    const std::string group = "224.0.31.1:14310";
    // Each day's file starts the evening before, so the files overlap at 23:00-24:00.
    write_pcap(dir.path() + "/2026-11-06/" + group + "/" + group + ".pcap",
               {frame(day * SEC - 3600 * SEC, "224.0.31.1", 14310, 1), frame(day * SEC + 600 * SEC, "224.0.31.1", 14310, 2),
                frame(day * SEC + 82800 * SEC, "224.0.31.1", 14310, 3)});
    write_pcap(dir.path() + "/2026-11-07/" + group + "/" + group + ".pcap",
               {frame(day * SEC + 82800 * SEC, "224.0.31.1", 14310, 3), frame(day * SEC + 90000 * SEC, "224.0.31.1", 14310, 4)});
    PcapReplaySource source("t", dir.path(), day * SEC - 7200 * SEC, day * SEC + 100000 * SEC,
                            {{0, "224.0.31.1", 14310, {}}});
    source.start();
    const auto got = drainToEnd(source);
    ASSERT_EQ(got.size(), 5u - 1u);  // the 23:00 packet appears in both files and replays once
    EXPECT_EQ(got[0].tag, 1);
    EXPECT_EQ(got[3].tag, 4);
}

TEST(PcapReplaySourceTest, CountsTheFramesTheReaderCouldNotUse) {
    TempDir dir;
    TestFrame cut = frame(10 * SEC, "224.0.31.1", 14310, 1);
    cut.overclaim = 40;
    write_pcap(dir.path() + "/a.pcap", {cut, frame(20 * SEC, "224.0.31.1", 14310, 2)});
    PcapReplaySource source("t", "", 0, 100 * SEC, {{0, "224.0.31.1", 14310, {dir.path() + "/a.pcap"}}});
    source.start();
    EXPECT_EQ(drainToEnd(source).size(), 1u);
    EXPECT_EQ(source.truncated_frames(), 1u);
}

TEST(PcapReplaySourceTest, MissingFilesAreAnErrorNotAHang) {
    TempDir dir;
    PcapReplaySource source("t", dir.path(), 0, 100 * SEC, {{0, "224.0.31.1", 14310, {}}});
    source.start();
    const auto got = drainToEnd(source);
    EXPECT_TRUE(got.empty());
    EXPECT_TRUE(source.positioned());
    EXPECT_NE(source.error().find("no capture files"), std::string::npos);
}

TEST(PcapReplaySourceTest, FullQueueHoldsTheReaderUntilDrained) {
    TempDir dir;
    std::vector<TestFrame> frames;
    for (uint64_t i = 0; i < 2000; ++i) {
        frames.push_back(frame((i + 1) * SEC, "224.0.31.1", 14310, uint8_t(i)));
    }
    write_pcap(dir.path() + "/a.pcap", frames);
    PcapReplaySource source("t", "", 0, 5000 * SEC, {{0, "224.0.31.1", 14310, {dir.path() + "/a.pcap"}}},
                            /*max_queued_batches=*/2);
    source.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // The reader is parked well short of the end, and nothing is lost.
    EXPECT_FALSE(source.reader_done());
    EXPECT_EQ(drainToEnd(source).size(), 2000u);
}

TEST(PcapReplaySourceTest, StopWhileReaderIsBlockedReturns) {
    TempDir dir;
    std::vector<TestFrame> frames;
    for (uint64_t i = 0; i < 2000; ++i) {
        frames.push_back(frame((i + 1) * SEC, "224.0.31.1", 14310, 1));
    }
    write_pcap(dir.path() + "/a.pcap", frames);
    PcapReplaySource source("t", "", 0, 5000 * SEC, {{0, "224.0.31.1", 14310, {dir.path() + "/a.pcap"}}}, 1);
    source.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source.stop();  // must not deadlock
    SUCCEED();
}

TEST(ReplayPacerTest, SpeedOneWaitsForTheCaptureGap) {
    ReplayPacer pacer(1.0);
    pacer.anchor(100 * SEC, 5000 * SEC);
    EXPECT_TRUE(pacer.is_due(100 * SEC, 5000 * SEC));
    EXPECT_FALSE(pacer.is_due(102 * SEC, 5001 * SEC));
    EXPECT_TRUE(pacer.is_due(102 * SEC, 5002 * SEC));
    EXPECT_EQ(pacer.due_ns(102 * SEC), 5002 * SEC);
}

TEST(ReplayPacerTest, ANonFiniteSpeedMeansUnpaced) {
    for (const double speed : {std::nan(""), std::numeric_limits<double>::infinity(), -2.0}) {
        EXPECT_EQ(ReplayPacer(speed).speed(), 0.0);
    }
}

TEST(ReplayPacerTest, SpeedScalesTheGap) {
    ReplayPacer pacer(4.0);
    pacer.anchor(100 * SEC, 5000 * SEC);
    EXPECT_EQ(pacer.due_ns(108 * SEC), 5002 * SEC);
}

TEST(ReplayPacerTest, SpeedZeroNeverWaits) {
    ReplayPacer pacer(0);
    pacer.anchor(100 * SEC, 5000 * SEC);
    EXPECT_TRUE(pacer.is_due(100000 * SEC, 5000 * SEC));
    EXPECT_EQ(pacer.rebase_ns(100000 * SEC, 5000 * SEC + 7), 5000 * SEC + 7);
}

TEST(ReplayPacerTest, RebasedTimeKeepsCaptureSpacingAndNeverLeadsTheClock) {
    ReplayPacer pacer(1.0);
    pacer.anchor(100 * SEC, 5000 * SEC);
    EXPECT_EQ(pacer.rebase_ns(103 * SEC, 5003 * SEC + 500), 5003 * SEC);  // sent a little late
    EXPECT_EQ(pacer.rebase_ns(103 * SEC, 5001 * SEC), 5001 * SEC);        // asked early: clamped
}

TEST(PcapReplaySourceTest, SkipsFilesTheNextOneAlreadyCovers) {
    TempDir dir;
    // Day files overlap, and the later file holds every packet from the time it starts (800),
    // so d1 and d2 are only ever read to be thrown away. d4 starts after the window.
    write_pcap(dir.path() + "/d1.pcap", {frame(10 * SEC, "224.0.31.1", 14310, 1), frame(500 * SEC, "224.0.31.1", 14310, 2)});
    write_pcap(dir.path() + "/d2.pcap", {frame(400 * SEC, "224.0.31.1", 14310, 3), frame(900 * SEC, "224.0.31.1", 14310, 4)});
    write_pcap(dir.path() + "/d3.pcap",
               {frame(800 * SEC, "224.0.31.1", 14310, 5), frame(900 * SEC, "224.0.31.1", 14310, 4), frame(1200 * SEC, "224.0.31.1", 14310, 6)});
    write_pcap(dir.path() + "/d4.pcap", {frame(5000 * SEC, "224.0.31.1", 14310, 7)});
    PcapReplaySource source("t", "", 850 * SEC, 2000 * SEC,
                            {{0, "224.0.31.1", 14310,
                              {dir.path() + "/d1.pcap", dir.path() + "/d2.pcap", dir.path() + "/d3.pcap", dir.path() + "/d4.pcap"}}});
    source.start();
    const auto got = drainToEnd(source);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].tag, 4);
    EXPECT_EQ(got[1].tag, 6);
}
