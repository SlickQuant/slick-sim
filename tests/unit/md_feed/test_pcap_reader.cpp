#include <gtest/gtest.h>
#include <md_feed/pcap_reader.hpp>

#include "pcap_test_util.hpp"

#include <fstream>
#include <vector>

using slick::sim::md_feed::PcapPacket;
using slick::sim::md_feed::PcapReader;
using namespace slick::sim::md_feed::test;

namespace {

std::vector<PcapPacket> readAll(PcapReader &reader, std::vector<std::vector<uint8_t>> *payloads = nullptr) {
    std::vector<PcapPacket> out;
    PcapPacket p;
    while (reader.next(p)) {
        out.push_back(p);
        if (payloads) {
            payloads->emplace_back(p.data, p.data + p.size);
        }
    }
    return out;
}

}   // namespace

TEST(PcapReaderTest, ParseIpv4) {
    EXPECT_EQ(slick::sim::md_feed::parse_ipv4("224.0.31.1"), (224u << 24) | (31u << 8) | 1u);
    EXPECT_THROW((void)slick::sim::md_feed::parse_ipv4("224.0.31"), std::runtime_error);
    EXPECT_THROW((void)slick::sim::md_feed::parse_ipv4("224.0.31.256"), std::runtime_error);
    EXPECT_THROW((void)slick::sim::md_feed::parse_ipv4("224.0.31.1x"), std::runtime_error);
}

TEST(PcapReaderTest, ReadsNanosecondCapture) {
    TempDir dir;
    const std::string path = dir.path() + "/a.pcap";
    write_pcap(path, {{1'700'000'000'123'456'789ull, "224.0.31.1", 14310, {1, 2, 3}},
                      {1'700'000'000'223'456'789ull, "224.0.32.3", 15312, {9}}});
    PcapReader reader(path);
    std::vector<std::vector<uint8_t>> payloads;
    const auto got = readAll(reader, &payloads);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].ts_ns, 1'700'000'000'123'456'789ull);
    EXPECT_EQ(got[0].dst_addr, slick::sim::md_feed::parse_ipv4("224.0.31.1"));
    EXPECT_EQ(got[0].dst_port, 14310);
    EXPECT_EQ(payloads[0], (std::vector<uint8_t>{1, 2, 3}));
    EXPECT_EQ(got[1].dst_port, 15312);
    EXPECT_EQ(payloads[1], (std::vector<uint8_t>{9}));
    EXPECT_TRUE(reader.error().empty());
}

TEST(PcapReaderTest, ReadsMicrosecondCaptureAsNanoseconds) {
    TempDir dir;
    const std::string path = dir.path() + "/a.pcap";
    write_pcap(path, {{1'700'000'000'123'456'000ull, "224.0.30.30", 3000, {7}}}, /*micro=*/true);
    PcapReader reader(path);
    const auto got = readAll(reader);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].ts_ns, 1'700'000'000'123'456'000ull);
}

TEST(PcapReaderTest, SkipsVlanTagsAndNonUdpFrames) {
    TempDir dir;
    const std::string path = dir.path() + "/a.pcap";
    TestFrame igmp;
    igmp.ts_ns = 1'000'000'000;
    igmp.igmp = true;
    igmp.payload = {0x16, 0, 0, 0, 224, 0, 30, 30};
    TestFrame tagged;
    tagged.ts_ns = 2'000'000'000;
    tagged.vlan = true;
    tagged.payload = {4, 5, 6};
    write_pcap(path, {igmp, tagged});
    PcapReader reader(path);
    std::vector<std::vector<uint8_t>> payloads;
    const auto got = readAll(reader, &payloads);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].ts_ns, 2'000'000'000ull);
    EXPECT_EQ(payloads[0], (std::vector<uint8_t>{4, 5, 6}));
}

TEST(PcapReaderTest, CountsFramesCutShortButNotOtherProtocols) {
    TempDir dir;
    const std::string path = dir.path() + "/a.pcap";
    TestFrame igmp;
    igmp.ts_ns = 1'000'000'000;
    igmp.igmp = true;
    igmp.payload = {0x16, 0, 0, 0, 224, 0, 30, 30};
    TestFrame cut;
    cut.ts_ns = 2'000'000'000;
    cut.payload = {1, 2, 3};
    cut.overclaim = 50;   // the datagram says more than the capture kept
    TestFrame whole;
    whole.ts_ns = 3'000'000'000;
    whole.payload = {4};
    write_pcap(path, {igmp, cut, whole});
    PcapReader reader(path);
    const auto got = readAll(reader);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(reader.truncated(), 1u);
}

TEST(PcapReaderTest, ReadsZstdAndGzipStreams) {
    for (const char *tool : {"zstd", "gzip"}) {
        TempDir dir;
        const std::string plain = dir.path() + "/a.pcap";
        write_pcap(plain, {{5'000'000'000ull, "224.0.31.1", 14310, {1}}, {6'000'000'000ull, "224.0.31.1", 14310, {2}}});
        const std::string packed = compress(plain, tool);
        if (packed.empty()) {
            GTEST_SKIP() << tool << " is not installed";
        }
        PcapReader reader(packed);
        EXPECT_EQ(readAll(reader).size(), 2u) << tool;
        EXPECT_TRUE(reader.error().empty()) << tool;
    }
}

TEST(PcapReaderTest, StopsEarlyOnACompressedFileWithoutHanging) {
    TempDir dir;
    const std::string plain = dir.path() + "/a.pcap";
    std::vector<slick::sim::md_feed::test::TestFrame> frames;
    for (int i = 0; i < 2000; ++i) {
        frames.push_back({uint64_t(i + 1) * 1000, "224.0.31.1", 14310, std::vector<uint8_t>(1000, uint8_t(i))});
    }
    write_pcap(plain, frames);
    const std::string packed = compress(plain, "zstd");
    if (packed.empty()) {
        GTEST_SKIP() << "zstd is not installed";
    }
    PcapReader reader(packed);
    PcapPacket p;
    ASSERT_TRUE(reader.next(p));
    // The destructor closes the stream with the decompressor still writing, and reaps it.
}

TEST(PcapReaderTest, TruncatedCaptureReportsAnError) {
    TempDir dir;
    const std::string path = dir.path() + "/a.pcap";
    write_pcap(path, {{1'000'000'000, "224.0.31.1", 14310, {1, 2, 3, 4, 5, 6, 7, 8}},
                      {2'000'000'000, "224.0.31.1", 14310, {1, 2, 3, 4, 5, 6, 7, 8}}});
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 5);
    PcapReader reader(path);
    EXPECT_EQ(readAll(reader).size(), 1u);
    EXPECT_FALSE(reader.error().empty());
}

TEST(PcapReaderTest, DecompressorFailureAfterWholePacketsIsNotACleanEnd) {
    TempDir dir;
    const std::string plain = dir.path() + "/a.pcap";
    write_pcap(plain, {{1'000'000'000, "224.0.31.1", 14310, {1}}, {2'000'000'000, "224.0.31.1", 14310, {2}}});
    const std::string packed = compress(plain, "gzip");
    if (packed.empty()) {
        GTEST_SKIP() << "gzip is not installed";
    }
    // Spoil the CRC in the trailer: every packet is delivered whole, then gzip fails.
    {
        std::fstream f(packed, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(-8, std::ios::end);
        f.put('\x5a');
        f.put('\x5a');
    }
    PcapReader reader(packed);
    EXPECT_EQ(readAll(reader).size(), 2u);
    EXPECT_NE(reader.error().find("decompressor"), std::string::npos);
}

TEST(PcapReaderTest, MissingOrInvalidFileThrows) {
    TempDir dir;
    EXPECT_THROW(PcapReader(dir.path() + "/missing.pcap.zst"), std::runtime_error);
    const std::string junk = dir.path() + "/junk.pcap";
    std::ofstream(junk) << "not a capture at all, just text";
    EXPECT_THROW(PcapReader{junk}, std::runtime_error);
}
