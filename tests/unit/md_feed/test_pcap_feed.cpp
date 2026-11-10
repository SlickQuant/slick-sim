#include <exchange/pcap_exchange.hpp>
#include <gtest/gtest.h>
#include <md_feed/pcap_feed.hpp>

#include "pcap_test_util.hpp"

#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

using namespace slick::sim;
using namespace slick::sim::md_feed;
using namespace slick::sim::md_feed::test;

namespace {

constexpr uint64_t SEC = 1'000'000'000ull;
constexpr uint8_t SNAPSHOT_END = 0xFF;

uint64_t wall_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

/// A venue with no protocol: the snapshot line's 0xFF packet is "the book is built".
class FakeFeed : public PcapFeedBase {
public:
    struct Seen {
        uint8_t line;
        uint8_t tag;
        uint64_t stamp_ns;
        uint64_t at_ns;
    };

    using PcapFeedBase::PcapFeedBase;

    /// The event time a timer firing saw while replaying, and the wall clock then.
    struct TimerStamp {
        uint64_t stamp_ns;
        uint64_t at_ns;
    };
    std::vector<TimerStamp> timer_stamps;
    std::vector<Seen> seen;
    bool synced = false;

protected:
    void on_packet(uint8_t line, const uint8_t *data, size_t, uint64_t now_ns) override {
        seen.push_back({line, data[0], event_time_ns(), now_ns});
        if (line == 1 && data[0] == SNAPSHOT_END) {
            synced = true;
        }
    }
    void on_timer(uint64_t now_ns) override {
        if (state() == FeedState::REPLAYING) {
            timer_stamps.push_back({event_time_ns(), now_ns});
        }
    }
    bool synchronized() const override { return synced; }
};

struct Harness {
    TempDir dir;
    std::shared_ptr<ReplayGate> gate = std::make_shared<ReplayGate>();
    std::unique_ptr<FakeFeed> feed;
    std::unique_ptr<Client> client;

    /// Incremental line 0: tags 1..`incrementals` one `gap_ns` apart from t=10s. The
    /// snapshot (line 1) packet 0xFF lands at t=10s + 1.5 gaps, so tag 1 and 2 are
    /// consumed while syncing and the rest wait.
    explicit Harness(double speed, uint64_t gap_ns = SEC, int incrementals = 6) {
        std::vector<TestFrame> inc;
        for (int i = 1; i <= incrementals; ++i) {
            inc.push_back({10 * SEC + (i - 1) * gap_ns, "224.0.31.1", 14310, {static_cast<uint8_t>(i)}});
        }
        write_pcap(dir.path() + "/inc.pcap", inc);
        write_pcap(dir.path() + "/snap.pcap",
                   {{10 * SEC + gap_ns + gap_ns / 2, "224.0.32.3", 15312, {SNAPSHOT_END}}});
        PcapFeedConfig config;
        config.begin_ns = 0;
        config.end_ns = 1000 * SEC;
        config.speed = speed;
        config.control_port = 0;
        feed = std::make_unique<FakeFeed>(
            "fake", Venue::CME, config,
            std::vector<PcapLine>{{0, "224.0.31.1", 14310, {dir.path() + "/inc.pcap"}},
                                  {1, "224.0.32.3", 15312, {dir.path() + "/snap.pcap"}}},
            gate);
        feed->start();
        client = std::make_unique<Client>(feed->control_port());
    }

    ~Harness() { feed->stop(); }

    /// Polls the feed as the exchange thread would, and reads the client, until `done`.
    bool pump(const std::function<bool()> &done, std::chrono::milliseconds limit = std::chrono::seconds(5)) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            feed->poll(wall_ns());
            client->read();
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
};

}   // namespace

TEST(PcapFeedTest, SyncsThenHoldsUntilStarted) {
    Harness h(/*speed=*/0);
    ASSERT_TRUE(h.client->connected());
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    // The sync fed packets up to the snapshot's end and no further.
    ASSERT_EQ(h.feed->seen.size(), 3u);
    EXPECT_EQ(h.feed->seen.back().tag, SNAPSHOT_END);

    // Held: a while of polling, with a client connected, delivers nothing more.
    h.pump([] { return false; }, std::chrono::milliseconds(100));
    EXPECT_EQ(h.feed->seen.size(), 3u);
    EXPECT_EQ(h.feed->state(), FeedState::READY);
    EXPECT_FALSE(h.gate->is_open());
    EXPECT_TRUE(h.client->saw(FeedState::READY));
}

TEST(PcapFeedTest, StartReplaysTheRestToTheEnd) {
    Harness h(/*speed=*/0);
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    h.client->start_replay();
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::FINISHED && h.client->saw(FeedState::FINISHED); }));
    // 6 incrementals + the snapshot marker, each once.
    EXPECT_EQ(h.feed->seen.size(), 7u);
    EXPECT_TRUE(h.client->saw(FeedState::REPLAYING));
    EXPECT_EQ(h.client->statuses.back().progress_permille, 1000);
    EXPECT_TRUE(h.gate->is_open());
}

TEST(PcapFeedTest, StartBeforeReadyStartsItOnceReadyAndTheGateStaysClosedUntilThen) {
    Harness h(/*speed=*/0);
    ASSERT_TRUE(h.client->connected());
    h.client->start_replay();
    bool open_while_building = false;
    ASSERT_TRUE(h.pump([&] {
        const auto state = h.feed->state();
        if ((state == FeedState::LOADING || state == FeedState::SYNCING) && h.gate->is_open()) {
            open_while_building = true;
        }
        return state == FeedState::FINISHED;
    }));
    EXPECT_FALSE(open_while_building);
    EXPECT_EQ(h.feed->seen.size(), 7u);
    EXPECT_TRUE(h.gate->is_open());
}

TEST(PcapFeedTest, NewClientIsToldTheCurrentState) {
    Harness h(/*speed=*/0);
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    Client late(h.feed->control_port());
    ASSERT_TRUE(late.connected());
    ASSERT_TRUE(h.pump([&] {
        late.read();
        return !late.statuses.empty();
    }));
    EXPECT_EQ(late.statuses.front().state, FeedState::READY);
    EXPECT_STREQ(late.statuses.front().feed, "fake");
}

TEST(PcapFeedTest, PacedReplayWaitsOutTheCaptureGaps) {
    // 200 ms between incrementals, real time: tags 3..6 after the sync are 0.3 s to 0.8 s
    // of capture time past the snapshot marker, and the first is released when the replay starts.
    Harness h(/*speed=*/1.0, /*gap_ns=*/200'000'000ull);
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    h.client->start_replay();
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::FINISHED; }));
    // Tag 3 anchors the replay; tag 6 is three gaps later.
    const FakeFeed::Seen *three = nullptr, *six = nullptr;
    for (const auto &s : h.feed->seen) {
        if (s.line == 0 && s.tag == 3) three = &s;
        if (s.line == 0 && s.tag == 6) six = &s;
    }
    ASSERT_TRUE(three && six);
    EXPECT_GE(six->at_ns - three->at_ns, 580'000'000ull);
    EXPECT_LT(six->at_ns - three->at_ns, 900'000'000ull);
    // Events are stamped on the wall clock, spaced as the capture was, never ahead of it.
    EXPECT_NEAR(static_cast<double>(six->stamp_ns - three->stamp_ns), 600e6, 30e6);
    EXPECT_LE(six->stamp_ns, six->at_ns);
}

TEST(PcapFeedTest, TimerEventsAreStampedNowNotWithTheLastPacketsTime) {
    // Paced, so between two packets the last one's rebased time falls behind the clock.
    Harness h(/*speed=*/1.0, /*gap_ns=*/200'000'000ull);
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    h.client->start_replay();
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::FINISHED; }));
    ASSERT_GT(h.feed->timer_stamps.size(), 10u);
    for (const auto &t : h.feed->timer_stamps) {
        EXPECT_EQ(t.stamp_ns, t.at_ns);
    }
}

TEST(PcapFeedTest, AnythingButStartDisconnectsTheClient) {
    Harness h(/*speed=*/0);
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    // A frame of a type the channel does not take: the old subscribe request.
    h.client->send_raw(ControlMessageType{3}, 35);
    h.pump([] { return false; }, std::chrono::milliseconds(100));
    EXPECT_EQ(h.feed->state(), FeedState::READY);
    EXPECT_FALSE(h.gate->is_open());
    EXPECT_TRUE(h.client->closed());
}

TEST(PcapFeedTest, CaptureEndingBeforeTheBookIsBuiltIsAnError) {
    TempDir dir;
    write_pcap(dir.path() + "/inc.pcap", {{10 * SEC, "224.0.31.1", 14310, {1}}});
    write_pcap(dir.path() + "/snap.pcap", {{11 * SEC, "224.0.32.3", 15312, {7}}});  // never the end marker
    PcapFeedConfig config;
    config.end_ns = 100 * SEC;
    config.speed = 0;
    FakeFeed feed("fake", Venue::CME, config,
                  {{0, "224.0.31.1", 14310, {dir.path() + "/inc.pcap"}}, {1, "224.0.32.3", 15312, {dir.path() + "/snap.pcap"}}},
                  std::make_shared<ReplayGate>());
    feed.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (feed.state() != FeedState::ERROR && std::chrono::steady_clock::now() < deadline) {
        feed.poll(wall_ns());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(feed.state(), FeedState::ERROR);
    feed.stop();
}

TEST(PcapFeedTest, MissingFilesAreAnError) {
    TempDir dir;
    PcapFeedConfig config;
    config.root = dir.path();
    config.end_ns = 100 * SEC;
    FakeFeed feed("fake", Venue::CME, config, {{0, "224.0.31.1", 14310, {}}}, std::make_shared<ReplayGate>());
    feed.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (feed.state() != FeedState::ERROR && std::chrono::steady_clock::now() < deadline) {
        feed.poll(wall_ns());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(feed.state(), FeedState::ERROR);
    feed.stop();
}

TEST(PcapFeedConfigTest, ParsesTimesAndRejectsBadConfig) {
    EXPECT_EQ(parse_utc_time_ns("2026-11-06T14:00:00Z"), 1793973600ull * SEC);
    EXPECT_EQ(parse_utc_time_ns("2026-11-06T14:00:00.5"), 1793973600ull * SEC + 500'000'000ull);
    EXPECT_THROW((void)parse_utc_time_ns("2026-11-06 14:00"), std::runtime_error);
    EXPECT_THROW((void)parse_utc_time_ns("2026-13-06T14:00:00Z"), std::runtime_error);
    EXPECT_THROW((void)parse_utc_time_ns("2026-02-31T14:00:00Z"), std::runtime_error);   // impossible date
    EXPECT_THROW((void)parse_utc_time_ns("2026-11-06T10:-5:00Z"), std::runtime_error);
    EXPECT_THROW((void)parse_utc_time_ns("1969-12-31T23:00:00Z"), std::runtime_error);

    nlohmann::json ok = {{"begin", "2026-11-06T14:00:00Z"}, {"end", "2026-11-06T15:00:00Z"},
                         {"control_channel", {{"port", 7000}}}};
    const auto c = PcapFeedConfig::from_json(ok, "f");
    EXPECT_EQ(c.end_ns - c.begin_ns, 3600ull * SEC);
    EXPECT_EQ(c.speed, 1.0);
    EXPECT_EQ(c.control_port, 7000);

    auto no_control = ok;
    no_control.erase("control_channel");
    EXPECT_THROW((void)PcapFeedConfig::from_json(no_control, "f"), std::runtime_error);
    auto reversed = ok;
    reversed["end"] = "2026-11-06T13:00:00Z";
    EXPECT_THROW((void)PcapFeedConfig::from_json(reversed, "f"), std::runtime_error);
    for (const int port : {70000, -1}) {
        auto bad_port = ok;
        bad_port["control_channel"]["port"] = port;
        EXPECT_THROW((void)PcapFeedConfig::from_json(bad_port, "f"), std::runtime_error) << port;
    }
    auto negative = ok;
    negative["speed"] = -1;
    EXPECT_THROW((void)PcapFeedConfig::from_json(negative, "f"), std::runtime_error);
}

TEST(PcapFeedTest, StartOpensTheGateEvenWhenTheSyncConsumedTheLastPacket) {
    // Two incrementals, then the snapshot marker as the capture's final packet.
    Harness h(/*speed=*/0, /*gap_ns=*/SEC, /*incrementals=*/2);
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::READY; }));
    h.client->start_replay();
    ASSERT_TRUE(h.pump([&] { return h.feed->state() == FeedState::FINISHED; }));
    EXPECT_TRUE(h.gate->is_open());
}

TEST(ReplayGateTest, IsOpenOnlyForFramesWrittenAfterItOpened) {
    uint64_t write_position = 40;
    ReplayGate gate([&] { return write_position; });
    EXPECT_FALSE(gate.is_open_at(100));
    gate.open();
    write_position = 90;
    EXPECT_FALSE(gate.is_open_at(39));   // queued while the book was built
    EXPECT_TRUE(gate.is_open_at(40));
    EXPECT_TRUE(gate.is_open_at(500));
    gate.open();   // again: the position it recorded is kept
    EXPECT_FALSE(gate.is_open_at(39));
    EXPECT_FALSE(ReplayGate().is_open_at(0));
}

TEST(ReplayGateViewTest, PublishesOnceTheGateIsOpenForTheFrameBeingRead) {
    ReplayGateView none;
    EXPECT_TRUE(none.publishing());   // no gate: everything is published
    EXPECT_FALSE(none.opened_now());

    uint64_t write_position = 10;
    auto gate = std::make_shared<ReplayGate>([&] { return write_position; });
    ReplayGateView view;
    view.set(gate);
    EXPECT_TRUE(view.gated());
    EXPECT_FALSE(view.publishing());
    gate->open();
    view.set_position(9);
    EXPECT_FALSE(view.opened_now());   // still reading frames from before it opened
    view.set_position(10);
    EXPECT_TRUE(view.opened_now());
    EXPECT_TRUE(view.publishing());
    EXPECT_FALSE(view.opened_now());   // only once
}

TEST(PcapExchangeTest, HelpersReadTheInstanceConfig) {
    using namespace slick::sim::exch;
    const nlohmann::json on = {{"md_feeds", {{{"type", "t_pcap"}, {"control_channel", {{"port", 7000}}}}}}};
    nlohmann::json off = on;
    off["md_feeds"][0]["enabled"] = false;
    EXPECT_TRUE(has_enabled_feed(on, "t_pcap"));
    EXPECT_FALSE(has_enabled_feed(off, "t_pcap"));
    EXPECT_FALSE(has_enabled_feed(on, "other"));

    std::unordered_map<uint16_t, std::string> ports;
    claim_pcap_control_ports(ports, on, "t_pcap", "a", "t");
    EXPECT_THROW(claim_pcap_control_ports(ports, on, "t_pcap", "b", "t"), std::runtime_error);
    claim_pcap_control_ports(ports, off, "t_pcap", "b", "t");   // a disabled feed claims nothing

    EXPECT_NO_THROW(reject_pcap_with_live_feeds(true, 0, "t", "segment", "a"));
    EXPECT_NO_THROW(reject_pcap_with_live_feeds(false, 2, "t", "segment", "a"));
    EXPECT_THROW(reject_pcap_with_live_feeds(true, 1, "t", "segment", "a"), std::runtime_error);

    struct Event { uint64_t transact_time; };
    struct Feed { uint64_t event_time_ns() const { return 77; } };
    const Feed feed;
    EXPECT_EQ(replay_stamped(Event{5}, &feed).transact_time, 77u);
    EXPECT_EQ(replay_stamped(Event{5}, static_cast<const Feed *>(nullptr)).transact_time, 5u);
}
