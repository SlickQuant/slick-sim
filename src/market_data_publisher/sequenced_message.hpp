#pragma once

#include <charconv>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace slick::sim::md_publisher {

/// One fan-out payload, serialised once and stamped per subscriber.
///
/// Coinbase gives every subscriber its own `sequence_num`, so a market-data message
/// differs between them by a single integer - but nlohmann has to walk the whole
/// document to produce the text, so dumping inside the per-socket loop re-serialises
/// an identical body once per subscriber. Measured on a ten-level `l2_data` update
/// with 50 subscribers, dumping once and splicing the number in is ~47x faster.
///
/// Hyperliquid does not sequence per subscriber and already sends one shared payload
/// to every socket, so this is only needed on the Coinbase side.
///
/// Construction rewrites `sequence_num` in the document, so the json passed in is
/// consumed - build the message, then wrap it, then fan out.
class SequencedMessage {
public:
    explicit SequencedMessage(nlohmann::json &message) {
        message["sequence_num"] = 0;
        prefix_ = message.dump();

        static constexpr std::string_view key = "\"sequence_num\":";
        const auto at = prefix_.find(key);
        if (at == std::string::npos) [[unlikely]] {
            // Nothing to stamp. Every fan-out message carries the field, but sending
            // the document unchanged is the safe answer if one ever does not.
            sequenced_ = false;
            return;
        }

        const auto value_at = at + key.size();
        auto end = value_at;
        while (end < prefix_.size() && prefix_[end] >= '0' && prefix_[end] <= '9') {
            ++end;
        }
        suffix_ = prefix_.substr(end);
        prefix_.resize(value_at);
    }

    /// The payload for one subscriber. The returned view is valid until the next
    /// call on this object, which is all `ws->send()` needs.
    std::string_view render(uint64_t sequence_num) {
        if (!sequenced_) [[unlikely]] {
            return prefix_;
        }
        char digits[20];
        const auto result = std::to_chars(digits, digits + sizeof(digits), sequence_num);
        buffer_.assign(prefix_);
        buffer_.append(digits, static_cast<size_t>(result.ptr - digits));
        buffer_.append(suffix_);
        return buffer_;
    }

private:
    /// Everything up to and including `"sequence_num":`; the whole document when
    /// there is no field to stamp.
    std::string prefix_;
    std::string suffix_;
    /// Reused across subscribers so the fan-out costs one allocation, not one per
    /// socket, once it has grown to the message's size.
    std::string buffer_;
    bool sequenced_ = true;
};

}   // end namespace slick::sim::md_publisher
