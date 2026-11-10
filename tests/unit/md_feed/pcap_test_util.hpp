#pragma once

#include <md_feed/pcap_reader.hpp>

#include <common/control_messages.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <cstring>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace slick::sim::md_feed::test {

/// A frame to write into a capture.
struct TestFrame {
    uint64_t ts_ns = 0;
    std::string address = "224.0.31.1";
    uint16_t port = 14310;
    std::vector<uint8_t> payload;
    bool vlan = false;
    bool igmp = false;  ///< An IP protocol 2 frame instead of UDP.
    uint16_t overclaim = 0;  ///< Bytes the UDP length claims beyond what was recorded.
};

/// A directory removed with its contents on destruction.
class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/slick_sim_pcap_XXXXXX";
        path_ = ::mkdtemp(pattern);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    [[nodiscard]] const std::string &path() const { return path_; }

private:
    std::string path_;
};

inline void put16be(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline std::vector<uint8_t> ethernet_frame(const TestFrame &f) {
    std::vector<uint8_t> v;
    const uint32_t group = parse_ipv4(f.address);
    // Multicast MAC, then a source MAC.
    v.insert(v.end(), {0x01, 0x00, 0x5e, uint8_t((group >> 16) & 0x7f), uint8_t(group >> 8), uint8_t(group)});
    v.insert(v.end(), {0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
    if (f.vlan) {
        put16be(v, 0x8100);
        put16be(v, 0x0064);
    }
    put16be(v, 0x0800);
    const uint16_t udp_len = static_cast<uint16_t>(8 + f.payload.size() + f.overclaim);
    const uint16_t total = static_cast<uint16_t>(20 + (f.igmp ? f.payload.size() : udp_len));
    v.push_back(0x45);
    v.push_back(0);
    put16be(v, total);
    put16be(v, 0);       // id
    put16be(v, 0x4000);  // DF, no fragment
    v.push_back(32);     // ttl
    v.push_back(f.igmp ? 2 : 17);
    put16be(v, 0);       // checksum, unchecked
    v.insert(v.end(), {205, 209, 221, 70});
    v.push_back(uint8_t(group >> 24));
    v.push_back(uint8_t(group >> 16));
    v.push_back(uint8_t(group >> 8));
    v.push_back(uint8_t(group));
    if (!f.igmp) {
        put16be(v, 40000);
        put16be(v, f.port);
        put16be(v, udp_len);
        put16be(v, 0);
    }
    v.insert(v.end(), f.payload.begin(), f.payload.end());
    return v;
}

/// Writes a classic little-endian nanosecond-resolution pcap (Ethernet link type).
/// `micro` writes the microsecond variant instead, with timestamps truncated to us.
inline void write_pcap(const std::string &path, const std::vector<TestFrame> &frames, bool micro = false) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    auto w32 = [&](uint32_t x) { out.write(reinterpret_cast<const char *>(&x), 4); };
    auto w16 = [&](uint16_t x) { out.write(reinterpret_cast<const char *>(&x), 2); };
    w32(micro ? 0xa1b2c3d4 : 0xa1b23c4d);
    w16(2);
    w16(4);
    w32(0);
    w32(0);
    w32(262144);
    w32(1);  // DLT_EN10MB
    for (const auto &f : frames) {
        const auto bytes = ethernet_frame(f);
        w32(static_cast<uint32_t>(f.ts_ns / 1'000'000'000ull));
        const uint64_t frac = f.ts_ns % 1'000'000'000ull;
        w32(static_cast<uint32_t>(micro ? frac / 1000 : frac));
        w32(static_cast<uint32_t>(bytes.size()));
        w32(static_cast<uint32_t>(bytes.size()));
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

/// Compresses `path` next to itself with `tool` ("zstd" or "gzip") and removes the original.
/// Returns the compressed file's path, or an empty string when the tool is unavailable.
inline std::string compress(const std::string &path, const std::string &tool) {
    const std::string cmd = tool + " -q -f -- '" + path + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        return {};
    }
    return path + (tool == "zstd" ? ".zst" : ".gz");
}

using slick::sim::ControlHeader;
using slick::sim::ControlMessageType;
using slick::sim::FeedState;
using slick::sim::FeedStatusMessage;
using slick::sim::StartMessage;

/// A control-channel client, polled from the test thread.
class Client {
public:
    explicit Client(uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connected_ = ::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    }
    ~Client() { ::close(fd_); }

    [[nodiscard]] bool connected() const { return connected_; }

    /// Asks the sim to start the replay.
    void start_replay() {
        StartMessage m{};
        m.header = {static_cast<uint16_t>(sizeof(m)), ControlMessageType::START};
        ASSERT_EQ(::send(fd_, &m, sizeof(m), 0), static_cast<ssize_t>(sizeof(m)));
    }

    /// Sends a frame of `type` and `length` bytes, the header and zeros.
    void send_raw(ControlMessageType type, uint16_t length) {
        std::vector<uint8_t> bytes(length, 0);
        const ControlHeader h{length, type};
        std::memcpy(bytes.data(), &h, sizeof(h));
        ASSERT_EQ(::send(fd_, bytes.data(), bytes.size(), 0), static_cast<ssize_t>(bytes.size()));
    }

    /// True once the sim has closed the connection (read() saw the end of the stream).
    [[nodiscard]] bool closed() const { return closed_; }

    /// Reads whatever has arrived, without blocking.
    void read() {
        uint8_t buf[4096];
        const ssize_t n = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
            buffer_.insert(buffer_.end(), buf, buf + n);
        } else if (n == 0) {
            closed_ = true;
        }
        while (buffer_.size() >= sizeof(ControlHeader)) {
            ControlHeader h;
            std::memcpy(&h, buffer_.data(), sizeof(h));
            if (buffer_.size() < h.length) {
                break;
            }
            if (h.type == ControlMessageType::FEED_STATUS) {
                FeedStatusMessage m;
                std::memcpy(&m, buffer_.data(), sizeof(m));
                statuses.push_back(m);
            }
            buffer_.erase(buffer_.begin(), buffer_.begin() + h.length);
        }
    }

    [[nodiscard]] bool saw(FeedState state) const {
        for (const auto &s : statuses) {
            if (s.state == state) {
                return true;
            }
        }
        return false;
    }

    std::vector<FeedStatusMessage> statuses;

private:
    int fd_;
    bool connected_ = false;
    bool closed_ = false;
    std::vector<uint8_t> buffer_;
};

}   // namespace slick::sim::md_feed::test
