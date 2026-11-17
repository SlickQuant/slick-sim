#pragma once

#include <common/control_messages.hpp>
#include <slick/logger.hpp>
#include <slick/queue.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace slick::sim::cli {

/// `<venue>/<instance>`, or `<venue>` alone for the one instance of a venue configured
/// without a list of them, which exch::expand_instances names after the venue.
inline std::string instance_path(std::string_view venue, std::string_view instance) {
    std::string path(venue);
    if (instance != venue) {
        path += '/';
        path += instance;
    }
    return path;
}

/// One command, as the CLI server hands it to the thread that runs it.
struct Command {
    uint64_t ticket;          ///< The server's id for the request, echoed in the reply.
    CliCommandType command;
    uint8_t flag;
    char arg[sizeof(CliRequestMessage::arg)];   ///< NUL-padded, as on the wire.

    [[nodiscard]] std::string_view arg_view() const noexcept { return {arg, strnlen(arg, sizeof(arg))}; }
};

/// Where the CLI server's commands reach a thread that owns some state (the main
/// thread, or a venue's order gateway), and where that thread's answers come back.
///
/// Two lock-free slick::queues, one each way. The CLI server thread is the only one
/// that posts and takes replies, the owner thread the only one that serves. post()
/// refuses while MAX_IN_FLIGHT commands are unanswered, so neither ring, lossy when
/// lapped, is lapped.
///
/// The owner calls serve() from its loop. With nothing waiting it is one acquire load.
class CliEndpoint {
public:
    static constexpr uint32_t MAX_IN_FLIGHT = 16;
    /// A longer reply is cut, ending in "...".
    static constexpr uint32_t MAX_REPLY_TEXT = 4096;

    explicit CliEndpoint(std::string name = {}) : name_(std::move(name)) {}
    CliEndpoint(const CliEndpoint &) = delete;
    CliEndpoint &operator=(const CliEndpoint &) = delete;

    /// How the CLI addresses it, `/`-separated from the venue down: `cme/<segment>`.
    [[nodiscard]] const std::string &name() const noexcept { return name_; }
    /// Before the endpoint is given to the CLI server.
    void set_name(std::string name) { name_ = std::move(name); }

    /// Owner thread. Runs `bool handler(const Command &, std::string &text)` on each
    /// command waiting, logs what it said and posts `text` back, OK when the handler
    /// returned true.
    template <typename Handler>
    void serve(Handler &&handler) {
        for (;;) {
            auto [command, count] = inbox_.read(inbox_cursor_);
            if (command == nullptr) {
                return;
            }
            text_.clear();
            const bool ok = handler(static_cast<const Command &>(*command), text_);
            LOG_INFO("CLI {} {}: {}", name_, ok ? "did" : "refused", text_);
            reply(command->ticket, ok ? CliStatus::OK : CliStatus::FAILED, text_);
        }
    }

    /// CLI server thread. False, posting nothing, while busy().
    [[nodiscard]] bool post(const Command &command) {
        if (busy()) {
            return false;
        }
        ++in_flight_;
        const auto index = inbox_.reserve();
        *inbox_[index] = command;
        inbox_.publish(index);
        return true;
    }

    /// CLI server thread. MAX_IN_FLIGHT commands posted and not yet answered through
    /// take_replies(), however long ago: one more would lap the inbox.
    [[nodiscard]] bool busy() const noexcept { return in_flight_ >= MAX_IN_FLIGHT; }

    /// CLI server thread. Runs `on_reply(uint64_t ticket, CliStatus, std::string_view)`
    /// on each reply waiting.
    template <typename OnReply>
    void take_replies(OnReply &&on_reply) {
        for (;;) {
            auto [data, size] = outbox_.read(outbox_cursor_);
            if (data == nullptr) {
                return;
            }
            --in_flight_;
            ReplyHeader header;
            std::memcpy(&header, data, sizeof(header));
            on_reply(header.ticket, header.status,
                     std::string_view(reinterpret_cast<const char *>(data) + sizeof(header), size - sizeof(header)));
        }
    }

private:
    struct ReplyHeader {
        uint64_t ticket;
        CliStatus status;
    };

    /// Room for MAX_IN_FLIGHT replies of the longest length, each rounded up to whole
    /// control slots.
    static constexpr uint32_t SLOT_BYTES = 64;
    static constexpr uint32_t OUTBOX_BYTES = 1u << 17;
    static_assert(OUTBOX_BYTES >= MAX_IN_FLIGHT * (sizeof(ReplyHeader) + MAX_REPLY_TEXT + SLOT_BYTES));

    void reply(uint64_t ticket, CliStatus status, std::string_view text) {
        const bool cut = text.size() > MAX_REPLY_TEXT;
        if (cut) {
            text = text.substr(0, MAX_REPLY_TEXT - 3);
        }
        const auto size = static_cast<uint32_t>(sizeof(ReplyHeader) + text.size() + (cut ? 3 : 0));
        const auto index = outbox_.reserve(size);
        uint8_t *out = outbox_[index];
        const ReplyHeader header{ticket, status};
        std::memcpy(out, &header, sizeof(header));
        std::memcpy(out + sizeof(header), text.data(), text.size());
        if (cut) {
            std::memcpy(out + sizeof(header) + text.size(), "...", 3);
        }
        outbox_.publish(index, size);
    }

    std::string name_;
    slick::queue<Command> inbox_{MAX_IN_FLIGHT};
    slick::queue<uint8_t> outbox_{OUTBOX_BYTES, SLOT_BYTES};
    uint64_t inbox_cursor_ = 0;    ///< Owner thread.
    std::string text_;             ///< Owner thread. Reused, so a reply allocates once.
    uint64_t outbox_cursor_ = 0;   ///< CLI server thread.
    uint32_t in_flight_ = 0;       ///< CLI server thread.
};

}   // namespace slick::sim::cli
