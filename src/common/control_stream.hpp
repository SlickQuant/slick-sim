#pragma once

#include "control_messages.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace slick::sim {

/// Cuts each connection's control-port byte stream into whole messages, keeping the
/// part of a message that has arrived until the rest does. For a TCP server that
/// speaks the ControlHeader framing. Server thread only.
class ControlStreams {
public:
    /// `max_length` is the longest message the server accepts. A longer header is
    /// refused as soon as it arrives rather than buffered for.
    explicit ControlStreams(size_t max_length) noexcept : max_length_(max_length) {}

    /// Hands `on_message(const ControlHeader &, const uint8_t *message)` every whole
    /// message `data` completes, header included, in order. False when the stream is
    /// malformed (a length out of range, or `on_message` returned false), and the
    /// caller should close the connection.
    template <typename OnMessage>
    bool feed(int connection, const uint8_t *data, size_t size, OnMessage &&on_message) {
        auto &buffer = partial_[connection];
        if (buffer.empty()) {
            // On a message boundary: read straight out of the socket buffer.
            const size_t used = split(data, size, on_message);
            if (used == SIZE_MAX) {
                return false;
            }
            if (used < size) {
                buffer.assign(data + used, data + size);
            }
            return true;
        }
        buffer.insert(buffer.end(), data, data + size);
        const size_t used = split(buffer.data(), buffer.size(), on_message);
        if (used == SIZE_MAX) {
            return false;
        }
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(used));
        return true;
    }

    void erase(int connection) { partial_.erase(connection); }

private:
    /// The bytes the whole messages at the front of `data` take, or SIZE_MAX.
    template <typename OnMessage>
    size_t split(const uint8_t *data, size_t size, OnMessage &on_message) {
        size_t used = 0;
        while (size - used >= sizeof(ControlHeader)) {
            ControlHeader header;
            std::memcpy(&header, data + used, sizeof(header));
            if (header.length < sizeof(ControlHeader) || header.length > max_length_) {
                return SIZE_MAX;
            }
            if (size - used < header.length) {
                break;
            }
            if (!on_message(header, data + used)) {
                return SIZE_MAX;
            }
            used += header.length;
        }
        return used;
    }

    size_t max_length_;
    std::unordered_map<int, std::vector<uint8_t>> partial_;
};

}   // namespace slick::sim
