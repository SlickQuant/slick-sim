#pragma once

#include <pcap/pcap.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

extern char **environ;

namespace slick::sim::md_feed {

/// One UDP datagram read from a capture. `data` points into the reader's buffer and
/// is valid until the next call to PcapReader::next().
struct PcapPacket {
    uint64_t ts_ns = 0;       ///< Capture time, nanoseconds since the epoch.
    uint32_t dst_addr = 0;    ///< IPv4 destination, host byte order.
    uint16_t dst_port = 0;
    const uint8_t *data = nullptr;  ///< UDP payload.
    size_t size = 0;
};

/// Packs a dotted quad such as "224.0.31.1" into host byte order, the form
/// PcapPacket::dst_addr uses. Throws on anything else.
[[nodiscard]] inline uint32_t parse_ipv4(const std::string &text) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 ||
        d > 255) {
        throw std::runtime_error(std::format("'{}' is not an IPv4 address", text));
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/// Reads UDP payloads, in capture order, out of one pcap or pcapng file.
///
/// `.zst` and `.gz` files are streamed through an external `zstd -dc` / `gzip -dc`, so
/// a multi-gigabyte day file is never held in memory or on disk. That stream cannot be
/// seeked: reaching a timestamp means reading up to it.
///
/// Classic pcap (micro or nanosecond) and pcapng are both read by libpcap and reported in
/// nanoseconds. Ethernet (with 802.1Q tags) and raw IP link types are understood. Frames
/// that are not whole IPv4 UDP datagrams (IGMP, fragments, other protocols) are skipped.
///
/// Not thread-safe; one thread owns a reader.
class PcapReader {
public:
    /// Opens `path`. Throws std::runtime_error when it cannot be opened or read as a capture.
    explicit PcapReader(const std::string &path) : path_(path) {
        char errbuf[PCAP_ERRBUF_SIZE] = {};
        FILE *file = open_stream(path);
        // pcap takes ownership of `file` and closes it with pcap_close().
        pcap_ = pcap_fopen_offline_with_tstamp_precision(file, PCAP_TSTAMP_PRECISION_NANO, errbuf);
        if (pcap_ == nullptr) {
            std::fclose(file);
            reap();
            throw std::runtime_error(std::format("{}: {}", path, errbuf));
        }
        link_type_ = pcap_datalink(pcap_);
        if (link_type_ != DLT_EN10MB && link_type_ != DLT_RAW && link_type_ != DLT_IPV4) {
            close();
            throw std::runtime_error(std::format("{}: unsupported link type {}", path, link_type_));
        }
    }

    ~PcapReader() { close(); }

    PcapReader(const PcapReader &) = delete;
    PcapReader &operator=(const PcapReader &) = delete;

    /// Reads the next UDP datagram. False at the end of the file; error() says whether
    /// that was a clean end.
    bool next(PcapPacket &out) {
        while (pcap_ != nullptr) {
            pcap_pkthdr *header = nullptr;
            const u_char *frame = nullptr;
            const int rc = pcap_next_ex(pcap_, &header, &frame);
            if (rc == 1) {
                if (parse(*header, frame, out)) {
                    return true;
                }
                continue;
            }
            if (rc == PCAP_ERROR) {
                error_ = pcap_geterr(pcap_);
            }
            // PCAP_ERROR_BREAK (-2) is the end of the file. For a decompressed one, that is
            // only a clean end if the decompressor says so: a truncated or corrupt archive
            // that stops on a packet boundary reads the same to libpcap.
            if (rc == PCAP_ERROR_BREAK && child_ > 0 && error_.empty()) {
                int status = 0;
                ::waitpid(child_, &status, 0);
                child_ = -1;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    error_ = WIFEXITED(status)
                        ? std::format("decompressor exited with status {}; the file is truncated or corrupt", WEXITSTATUS(status))
                        : std::string("decompressor was killed; the file is truncated or corrupt");
                }
            }
            return false;
        }
        return false;
    }

    /// Empty after a clean end of file; otherwise what stopped the read, for example a
    /// capture truncated mid-packet.
    [[nodiscard]] const std::string &error() const { return error_; }
    [[nodiscard]] const std::string &path() const { return path_; }
    /// Frames skipped so far for being cut short or malformed - not those that are simply
    /// not UDP (IGMP, ARP, fragments of other protocols).
    [[nodiscard]] uint64_t truncated() const noexcept { return truncated_; }

private:
    bool cut_short() noexcept {
        ++truncated_;
        return false;
    }

    bool parse(const pcap_pkthdr &header, const u_char *frame, PcapPacket &out) {
        const uint8_t *p = frame;
        size_t left = header.caplen;
        if (link_type_ == DLT_EN10MB) {
            if (left < 14) {
                return cut_short();
            }
            uint16_t type = static_cast<uint16_t>((p[12] << 8) | p[13]);
            p += 14;
            left -= 14;
            while ((type == 0x8100 || type == 0x88a8) && left >= 4) {
                type = static_cast<uint16_t>((p[2] << 8) | p[3]);
                p += 4;
                left -= 4;
            }
            if (type != 0x0800) {
                return false;
            }
        }
        if (left < 20) {
            return cut_short();
        }
        if ((p[0] >> 4) != 4) {
            return false;
        }
        const size_t ihl = static_cast<size_t>(p[0] & 0x0f) * 4;
        const size_t total = static_cast<size_t>((p[2] << 8) | p[3]);
        const uint16_t frag = static_cast<uint16_t>(((p[6] << 8) | p[7]) & 0x3fff);  // MF flag + offset
        // Only whole datagrams: a fragment has no UDP header to read, or no end.
        if (p[9] != 17 || frag != 0) {
            return false;
        }
        if (ihl < 20 || left < ihl + 8 || total < ihl + 8) {
            return cut_short();
        }
        const uint8_t *udp = p + ihl;
        const size_t udp_len = static_cast<size_t>((udp[4] << 8) | udp[5]);
        if (udp_len < 8) {
            return cut_short();
        }
        // A capture can cut a frame short; trust no more than was recorded.
        size_t payload = udp_len - 8;
        if (payload > left - ihl - 8) {
            return cut_short();
        }
        // The reader asked libpcap for nanosecond precision, so tv_usec holds nanoseconds.
        out.ts_ns = static_cast<uint64_t>(header.ts.tv_sec) * 1'000'000'000ull + static_cast<uint64_t>(header.ts.tv_usec);
        out.dst_addr = (uint32_t(p[16]) << 24) | (uint32_t(p[17]) << 16) | (uint32_t(p[18]) << 8) | p[19];
        out.dst_port = static_cast<uint16_t>((udp[2] << 8) | udp[3]);
        out.data = udp + 8;
        out.size = payload;
        return true;
    }

    /// A FILE* over the capture: the file itself, or the output of a decompressor.
    FILE *open_stream(const std::string &path) {
        // Fail with the real reason here; a decompressor handed a missing file only
        // produces an empty stream, which libpcap reports as a bad capture.
        if (::access(path.c_str(), R_OK) != 0) {
            throw std::runtime_error(std::format("{}: {}", path, std::strerror(errno)));
        }
        const char *tool = nullptr;
        if (path.ends_with(".zst")) {
            tool = "zstd";
        } else if (path.ends_with(".gz")) {
            tool = "gzip";
        }
        if (tool == nullptr) {
            FILE *file = std::fopen(path.c_str(), "rb");
            if (file == nullptr) {
                throw std::runtime_error(std::format("{}: {}", path, std::strerror(errno)));
            }
            return file;
        }
        // Spawned directly, not through popen: pcap_close() closes the FILE* itself, and a
        // popen stream must be pclose()d. The child is reaped by reap() instead.
        int fds[2];
        if (::pipe2(fds, O_CLOEXEC) != 0) {
            throw std::runtime_error(std::format("{}: pipe: {}", path, std::strerror(errno)));
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        const char *argv[] = {tool, "-dc", "--", path.c_str(), nullptr};
        const int rc = ::posix_spawnp(&child_, tool, &actions, nullptr, const_cast<char *const *>(argv), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(fds[1]);
        if (rc != 0) {
            ::close(fds[0]);
            child_ = -1;
            throw std::runtime_error(std::format("{}: cannot run {}: {}", path, tool, std::strerror(rc)));
        }
        FILE *file = ::fdopen(fds[0], "rb");
        if (file == nullptr) {
            ::close(fds[0]);
            reap();
            throw std::runtime_error(std::format("{}: fdopen: {}", path, std::strerror(errno)));
        }
        return file;
    }

    void close() {
        if (pcap_ != nullptr) {
            pcap_close(pcap_);  // also closes the stream, which stops a decompressor still writing
            pcap_ = nullptr;
        }
        reap();
    }

    void reap() {
        if (child_ > 0) {
            ::waitpid(child_, nullptr, 0);
            child_ = -1;
        }
    }

    std::string path_;
    pcap_t *pcap_ = nullptr;
    int link_type_ = 0;
    pid_t child_ = -1;
    std::string error_;
    uint64_t truncated_ = 0;
};

}   // namespace slick::sim::md_feed
