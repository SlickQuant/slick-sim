// First: slick-socket logs only through LOG_* macros defined before it is included.
#include <slick/logger.hpp>
#include "cli_server.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>
#include <thread>

namespace slick::sim::cli {

namespace {

std::string_view field(const char *text, size_t size) {
    return {text, strnlen(text, size)};
}

bool known(CliCommandType command) {
    const auto value = static_cast<uint8_t>(command);
    return value >= static_cast<uint8_t>(CliCommandType::LIST_TARGETS) &&
           value <= static_cast<uint8_t>(CliCommandType::LIST_REJECTS);
}

}   // namespace

CliServer::CliServer(const slick::socket::TCPServerConfig &config, std::chrono::milliseconds reply_timeout)
    : Server("CLI", config)
    , reply_timeout_(reply_timeout)
{
}

CliServer::~CliServer() {
    stop();
}

void CliServer::add_endpoints(const std::string &group, const std::vector<CliEndpoint *> &endpoints) {
    auto route = [this](std::string_view target, size_t index) {
        if (target.empty()) {
            return;
        }
        if (target.size() > sizeof(CliRequestMessage::target)) {
            LOG_WARN("CLI: target {} is longer than a request can name", target);
        }
        auto it = targets_.find(target);
        if (it == targets_.end()) {
            it = targets_.emplace(std::string(target), std::vector<size_t>{}).first;
        }
        if (std::find(it->second.begin(), it->second.end(), index) == it->second.end()) {
            it->second.push_back(index);
        }
    };
    for (CliEndpoint *endpoint : endpoints) {
        auto found = std::find(endpoints_.begin(), endpoints_.end(), endpoint);
        const auto index = static_cast<size_t>(found - endpoints_.begin());
        if (found == endpoints_.end()) {
            endpoints_.push_back(endpoint);
        }
        route(group, index);
        const std::string_view name = endpoint->name();
        for (size_t slash = name.find('/'); slash != std::string_view::npos; slash = name.find('/', slash + 1)) {
            route(name.substr(0, slash), index);
        }
        route(name, index);
    }
}

bool CliServer::start() {
    if (!Server::start()) {
        LOG_ERROR("CLI failed to start on port {}", Server::port());
        return false;
    }
    LOG_INFO("CLI listening on port {}, {} targets", Server::port(), targets_.size());
    return true;
}

void CliServer::stop() {
    Server::stop();
}

void CliServer::flush(std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (pending_count_.load(std::memory_order_acquire) != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void CliServer::onClientConnected(int client_id, const std::string &client_address) {
    LOG_INFO("CLI: connection {} from {}", client_id, client_address);
    clients_[client_id];
}

void CliServer::onClientDisconnected(int client_id) {
    LOG_INFO("CLI: connection {} closed", client_id);
    streams_.erase(client_id);
    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    // The answer still comes; it just has no one to go to.
    if (auto pending = pending_.find(it->second.ticket); pending != pending_.end()) {
        pending->second.client_id = -1;
    }
    clients_.erase(it);
}

void CliServer::onClientData(int client_id, const uint8_t *data, size_t length) {
    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    auto &client = it->second;
    const bool ok = streams_.feed(client_id, data, length,
                                  [&client](const ControlHeader &header, const uint8_t *message) {
        if (header.type != ControlMessageType::CLI_REQUEST || header.length != sizeof(CliRequestMessage) ||
            client.waiting.size() >= MAX_WAITING) {
            return false;
        }
        std::memcpy(&client.waiting.emplace_back(), message, sizeof(CliRequestMessage));
        return true;
    });
    if (!ok) {
        LOG_WARN("CLI: malformed message or too many requests from connection {}, closing it", client_id);
        client.waiting.clear();
        Server::disconnect_client(client_id);
        return;
    }
    dispatch(client_id);
}

void CliServer::onPoll() {
    for (size_t i = 0; i < endpoints_.size(); ++i) {
        endpoints_[i]->take_replies([this, i](uint64_t ticket, CliStatus status, std::string_view text) {
            on_reply(i, ticket, status, text);
        });
    }
    if (pending_.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    expired_.clear();
    for (const auto &[ticket, pending] : pending_) {
        if (now >= pending.deadline) {
            expired_.push_back(ticket);
        }
    }
    for (uint64_t ticket : expired_) {
        finish(ticket);
    }
}

void CliServer::dispatch(int client_id) {
    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    auto &client = it->second;
    while (client.ticket == 0 && !client.waiting.empty()) {
        const CliRequestMessage request = client.waiting.front();
        client.waiting.pop_front();
        const auto target = field(request.target, sizeof(request.target));
        const auto arg = field(request.arg, sizeof(request.arg));
        // Copies: a packed field cannot bind to the logger's references.
        const uint32_t request_id = request.request_id;
        const int flag = request.flag;
        LOG_INFO("CLI: connection {} request {}: {} {} {}{}{}", client_id, request_id, target,
                 to_string(request.command), flag, arg.empty() ? "" : " ", arg);

        if (request.command == CliCommandType::LIST_TARGETS) {
            send_reply(client_id, request.request_id, CliStatus::OK, list_targets());
            continue;
        }
        if (!known(request.command)) {
            send_reply(client_id, request.request_id, CliStatus::FAILED,
                       std::format("unknown command {}", static_cast<int>(request.command)));
            continue;
        }
        auto route = targets_.find(target);
        if (route == targets_.end()) {
            send_reply(client_id, request.request_id, CliStatus::FAILED,
                       std::format("unknown target '{}'; ask for the targets", target));
            continue;
        }
        const auto &indices = route->second;
        if (std::any_of(indices.begin(), indices.end(), [this](size_t i) { return endpoints_[i]->busy(); })) {
            send_reply(client_id, request.request_id, CliStatus::FAILED, std::format("'{}' is busy", target));
            continue;
        }

        const uint64_t ticket = next_ticket_++;
        Command command{ticket, request.command, request.flag, {}};
        std::memcpy(command.arg, request.arg, sizeof(command.arg));
        Pending pending{client_id, request.request_id, {}, indices.size(),
                        std::chrono::steady_clock::now() + reply_timeout_};
        pending.answers.reserve(indices.size());
        for (size_t index : indices) {
            pending.answers.push_back(Answer{index, false, CliStatus::FAILED, {}});
            // None was busy, and a target names each endpoint once.
            [[maybe_unused]] const bool posted = endpoints_[index]->post(command);
            assert(posted);
        }
        pending_.emplace(ticket, std::move(pending));
        pending_count_.store(pending_.size(), std::memory_order_release);
        client.ticket = ticket;
    }
}

void CliServer::on_reply(size_t endpoint, uint64_t ticket, CliStatus status, std::string_view text) {
    auto it = pending_.find(ticket);
    if (it == pending_.end()) {
        return;   // Timed out; the client has had its answer.
    }
    for (auto &answer : it->second.answers) {
        if (answer.endpoint == endpoint && !answer.answered) {
            answer.answered = true;
            answer.status = status;
            answer.text.assign(text);
            if (--it->second.remaining == 0) {
                finish(ticket);
            }
            return;
        }
    }
}

void CliServer::finish(uint64_t ticket) {
    auto it = pending_.find(ticket);
    if (it == pending_.end()) {
        return;
    }
    Pending pending = std::move(it->second);
    pending_.erase(it);

    CliStatus status = CliStatus::OK;
    std::string text;
    for (const auto &answer : pending.answers) {
        const auto &name = endpoints_[answer.endpoint]->name();
        if (!answer.answered) {
            status = CliStatus::FAILED;
            text += std::format("{}: no answer within {} ms\n", name, reply_timeout_.count());
            LOG_WARN("CLI: {} did not answer request {} within {} ms", name, pending.request_id,
                     reply_timeout_.count());
            continue;
        }
        if (answer.status != CliStatus::OK) {
            status = CliStatus::FAILED;
        }
        std::string_view rest = answer.text;
        do {
            const size_t eol = rest.find('\n');
            text += name;
            text += ": ";
            text += rest.substr(0, eol);
            text += '\n';
            rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 1);
        } while (!rest.empty());
    }
    if (!text.empty()) {
        text.pop_back();
    }

    if (pending.client_id >= 0) {
        send_reply(pending.client_id, pending.request_id, status, text);
        if (auto client = clients_.find(pending.client_id); client != clients_.end()) {
            client->second.ticket = 0;
        }
    }
    pending_count_.store(pending_.size(), std::memory_order_release);
    if (pending.client_id >= 0) {
        dispatch(pending.client_id);
    }
}

void CliServer::send_reply(int client_id, uint32_t request_id, CliStatus status, std::string_view text) {
    constexpr size_t max_text = UINT16_MAX - sizeof(CliReplyMessage);
    if (text.size() > max_text) {
        text = text.substr(0, max_text);
    }
    const CliReplyMessage reply{{static_cast<uint16_t>(sizeof(CliReplyMessage) + text.size()),
                                 ControlMessageType::CLI_REPLY},
                                request_id, status};
    reply_.resize(sizeof(reply) + text.size());
    std::memcpy(reply_.data(), &reply, sizeof(reply));
    std::memcpy(reply_.data() + sizeof(reply), text.data(), text.size());
    Server::send_data(client_id, reply_.data(), reply_.size());
}

std::string CliServer::list_targets() const {
    std::string out;
    for (const auto &[name, indices] : targets_) {
        if (!out.empty()) {
            out += '\n';
        }
        out += name;
        if (indices.size() > 1) {
            out += std::format(" ({} endpoints)", indices.size());
        }
    }
    return out;
}

}   // namespace slick::sim::cli
