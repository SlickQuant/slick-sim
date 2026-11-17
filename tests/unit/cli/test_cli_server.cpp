#include <gtest/gtest.h>
#include <slick/logger.hpp>
#include <cli/cli_server.hpp>
#include <slick/socket/polled_tcp_client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace slick::sim;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// The CLI server over a real loopback socket, with endpoints served on threads of
// their own as a gateway's would be.
// ---------------------------------------------------------------------------
namespace {

using Handler = std::function<bool(const cli::Command &, std::string &)>;

/// "<command> <flag> <arg>", OK.
bool echo(const cli::Command &command, std::string &out) {
    out = std::format("{} {} {}", to_string(command.command), static_cast<int>(command.flag), command.arg_view());
    return true;
}

/// Whether `done()` comes true within 5 s.
bool eventually(const std::function<bool()> &done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

/// A handler that holds its endpoint's thread in the first command until open(), up to
/// 5 s, then says back each command's arg.
class Gate {
public:
    Handler handler() {
        return [this](const cli::Command &command, std::string &out) {
            entered_ = true;
            eventually([this]() { return open_.load(); });
            out = command.arg_view();
            return true;
        };
    }
    [[nodiscard]] bool entered() const { return entered_.load(); }
    void open() { open_ = true; }

private:
    std::atomic_bool entered_{false};
    std::atomic_bool open_{false};
};

/// An endpoint, and the thread that serves it once serve() is called.
class Served {
public:
    explicit Served(std::string name, Handler handler = echo) : endpoint(std::move(name)), handler_(std::move(handler)) {}
    ~Served() {
        run_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void serve() {
        thread_ = std::thread([this]() {
            while (run_.load(std::memory_order_relaxed)) {
                endpoint.serve(handler_);
                std::this_thread::sleep_for(1ms);
            }
        });
    }

    cli::CliEndpoint endpoint;

private:
    Handler handler_;
    std::atomic_bool run_{true};
    std::thread thread_;
};

struct Reply {
    uint32_t request_id;
    CliStatus status;
    std::string text;
};

class Client {
public:
    explicit Client(uint16_t port) : client_("cli-test", make_config(port)) {
        client_.connect();
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (client_.poll() == slick::socket::PolledTCPClient::State::Connecting &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
    }

    [[nodiscard]] bool connected() const { return client_.is_connected(); }

    static std::vector<uint8_t> request(uint32_t request_id, std::string_view target, CliCommandType command,
                                        uint8_t flag = 0, std::string_view arg = {}) {
        CliRequestMessage message{};
        message.header = {sizeof(CliRequestMessage), ControlMessageType::CLI_REQUEST};
        message.request_id = request_id;
        message.command = command;
        message.flag = flag;
        std::memcpy(message.target, target.data(), std::min(target.size(), sizeof(message.target)));
        std::memcpy(message.arg, arg.data(), std::min(arg.size(), sizeof(message.arg)));
        std::vector<uint8_t> bytes(sizeof(message));
        std::memcpy(bytes.data(), &message, sizeof(message));
        return bytes;
    }

    bool send(std::span<const uint8_t> bytes) { return client_.send(bytes); }
    void disconnect() { client_.disconnect(); }

    /// Sends one request and waits for the next reply.
    std::optional<Reply> call(std::string_view target, CliCommandType command, uint8_t flag = 0,
                              std::string_view arg = {}) {
        const uint32_t id = next_id_++;
        if (!send(request(id, target, command, flag, arg))) {
            return std::nullopt;
        }
        auto reply = read();
        EXPECT_TRUE(reply && reply->request_id == id);
        return reply;
    }

    /// The next reply, within 5 s.
    std::optional<Reply> read() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (stream_.size() >= sizeof(CliReplyMessage)) {
                CliReplyMessage header;
                std::memcpy(&header, stream_.data(), sizeof(header));
                if (stream_.size() >= header.header.length) {
                    Reply reply{header.request_id, header.status,
                                std::string(reinterpret_cast<const char *>(stream_.data()) + sizeof(header),
                                            header.header.length - sizeof(header))};
                    stream_.erase(stream_.begin(), stream_.begin() + header.header.length);
                    return reply;
                }
            }
            if (pump() < 0) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// Whether the server closes the connection within 5 s.
    bool closed() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pump() < 0) {
                return true;
            }
        }
        return false;
    }

private:
    static slick::socket::PolledTCPClientConfig make_config(uint16_t port) {
        slick::socket::PolledTCPClientConfig config;
        config.server_address = "127.0.0.1";
        config.server_port = port;
        return config;
    }

    std::ptrdiff_t pump() {
        uint8_t buffer[4096];
        const auto n = client_.receive(buffer);
        if (n > 0) {
            stream_.insert(stream_.end(), buffer, buffer + n);
        } else if (n == 0) {
            std::this_thread::sleep_for(1ms);
        }
        return n;
    }

    slick::socket::PolledTCPClient client_;
    std::vector<uint8_t> stream_;
    uint32_t next_id_ = 1;
};

std::unique_ptr<cli::CliServer> make_server(std::chrono::milliseconds reply_timeout = 2s) {
    slick::socket::TCPServerConfig config;
    config.port = 0;
    return std::make_unique<cli::CliServer>(config, reply_timeout);
}

}   // namespace

TEST(CliServerTest, ListsItsTargetsItself) {
    Served sim("sim"), a("cme/a"), b("cme/b");
    auto server = make_server();
    server->add_endpoints("sim", {&sim.endpoint});
    server->add_endpoints("cme", {&a.endpoint, &b.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());
    ASSERT_TRUE(client.connected());

    auto reply = client.call("", CliCommandType::LIST_TARGETS);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    EXPECT_EQ(reply->text, "cme (2 endpoints)\ncme/a\ncme/b\nsim");
    server->stop();
}

TEST(CliServerTest, ACommandReachesItsTargetsEndpoint) {
    Served sim("sim");
    sim.serve();
    auto server = make_server();
    server->add_endpoints("sim", {&sim.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    auto reply = client.call("sim", CliCommandType::SET_LOG_LEVEL, 0, "debug");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    EXPECT_EQ(reply->text, "sim: log_level 0 debug");
    server->stop();
}

// A venue key reaches every endpoint under it and is answered once, a line per endpoint
// in the order they were added, OK only if all of them were. A `/`-prefix narrows it.
TEST(CliServerTest, AGroupFansOutAndIsAnsweredOnce) {
    Served one("ice/s1/bgw0"), two("ice/s1/bgw1", [](const cli::Command &, std::string &out) {
        out = "first line\nsecond line";
        return false;
    }), other("ice/s2/bgw0");
    one.serve();
    two.serve();
    other.serve();
    auto server = make_server();
    server->add_endpoints("ice", {&one.endpoint, &two.endpoint, &other.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    auto reply = client.call("ice", CliCommandType::REJECT_LOGON, 1, "GW1");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::FAILED);
    EXPECT_EQ(reply->text, "ice/s1/bgw0: reject_logon 1 GW1\n"
                           "ice/s1/bgw1: first line\n"
                           "ice/s1/bgw1: second line\n"
                           "ice/s2/bgw0: reject_logon 1 GW1");

    reply = client.call("ice/s2", CliCommandType::REJECT_LOGON, 0, "GW1");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    EXPECT_EQ(reply->text, "ice/s2/bgw0: reject_logon 0 GW1");
    server->stop();
}

TEST(CliServerTest, AnUnknownTargetOrCommandIsRefusedAndTheConnectionKept) {
    Served sim("sim");
    sim.serve();
    auto server = make_server();
    server->add_endpoints("sim", {&sim.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    auto reply = client.call("nyse", CliCommandType::REJECT_LOGON, 1, "X");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::FAILED);
    EXPECT_EQ(reply->text, "unknown target 'nyse'; ask for the targets");

    reply = client.call("sim", static_cast<CliCommandType>(99));
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::FAILED);
    EXPECT_EQ(reply->text, "unknown command 99");

    reply = client.call("sim", CliCommandType::SHUTDOWN);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    server->stop();
}

// An endpoint whose thread does not serve it fails the command after the timeout. Its
// late answer is dropped, and once it serves again it answers the next command.
TEST(CliServerTest, ASilentEndpointTimesOutThenRecovers) {
    Served stuck("cme");
    auto server = make_server(200ms);
    server->add_endpoints("cme", {&stuck.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    auto reply = client.call("cme", CliCommandType::REJECT_LOGON, 1, "first");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::FAILED);
    EXPECT_EQ(reply->text, "cme: no answer within 200 ms");

    stuck.serve();
    reply = client.call("cme", CliCommandType::REJECT_LOGON, 1, "second");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    EXPECT_EQ(reply->text, "cme: reject_logon 1 second");
    server->stop();
}

// A timed-out command still waits in the endpoint's inbox, so it keeps its slot until
// the endpoint answers. A stuck endpoint turns busy after MAX_IN_FLIGHT of them, so
// nothing overwrites its ring, and once it serves again it carries out every one, in order.
TEST(CliServerTest, TimedOutCommandsKeepTheirSlotsUntilAnswered) {
    std::mutex mutex;
    std::vector<std::string> served;
    Served stuck("cme", [&](const cli::Command &command, std::string &out) {
        std::lock_guard lock(mutex);
        served.emplace_back(command.arg_view());
        out = "done";
        return true;
    });
    auto server = make_server(20ms);
    server->add_endpoints("cme", {&stuck.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    std::vector<std::string> expected;
    for (uint32_t i = 0; i < cli::CliEndpoint::MAX_IN_FLIGHT; ++i) {
        expected.push_back(std::format("c{}", i));
        auto reply = client.call("cme", CliCommandType::REJECT_LOGON, 1, expected.back());
        ASSERT_TRUE(reply);
        EXPECT_EQ(reply->text, "cme: no answer within 20 ms");
    }
    auto reply = client.call("cme", CliCommandType::REJECT_LOGON, 1, "over");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::FAILED);
    EXPECT_EQ(reply->text, "'cme' is busy");

    // Busy until the server has taken all the late answers. Only that refusal is retried:
    // once posted, "after" runs, though on a loaded machine its answer can miss the 20 ms.
    stuck.serve();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    do {
        reply = client.call("cme", CliCommandType::REJECT_LOGON, 1, "after");
        ASSERT_TRUE(reply);
    } while (reply->text == "'cme' is busy" && std::chrono::steady_clock::now() < deadline);
    ASSERT_NE(reply->text, "'cme' is busy");
    expected.push_back("after");
    EXPECT_TRUE(eventually([&]() {
        std::lock_guard lock(mutex);
        return served.size() >= expected.size();
    }));
    std::lock_guard lock(mutex);
    EXPECT_EQ(served, expected);
    server->stop();
}

// One client waiting on a slow endpoint holds up no other client.
TEST(CliServerTest, ClientsAreAnsweredIndependently) {
    Gate gate;
    Served slow("cme", gate.handler()), sim("sim");
    slow.serve();
    sim.serve();
    auto server = make_server();
    server->add_endpoints("cme", {&slow.endpoint});
    server->add_endpoints("sim", {&sim.endpoint});
    ASSERT_TRUE(server->start());
    Client waiting(server->listen_port()), other(server->listen_port());

    ASSERT_TRUE(waiting.send(Client::request(1, "cme", CliCommandType::REJECT_LOGON, 1, "slow")));
    ASSERT_TRUE(eventually([&]() { return gate.entered(); }));
    auto reply = other.call("sim", CliCommandType::SET_LOG_LEVEL, 0, "info");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->text, "sim: log_level 0 info");

    gate.open();
    reply = waiting.read();
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->request_id, 1u);
    EXPECT_EQ(reply->text, "cme: slow");
    server->stop();
}

// A client that leaves while its command runs: the answer goes nowhere, and the next
// client's command is served.
TEST(CliServerTest, AnAnswerToAClientThatLeftIsDropped) {
    Gate gate;
    Served cme("cme", gate.handler());
    cme.serve();
    auto server = make_server();
    server->add_endpoints("cme", {&cme.endpoint});
    ASSERT_TRUE(server->start());

    Client gone(server->listen_port());
    ASSERT_TRUE(gone.send(Client::request(1, "cme", CliCommandType::REJECT_LOGON, 1, "gone")));
    ASSERT_TRUE(eventually([&]() { return gate.entered(); }));
    gone.disconnect();
    std::this_thread::sleep_for(50ms);   // For the server to see it go before the answer comes.
    gate.open();

    Client next(server->listen_port());
    auto reply = next.call("cme", CliCommandType::REJECT_LOGON, 1, "next");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    EXPECT_EQ(reply->text, "cme: next");
    server->stop();
}

// Behind the request being answered a client may queue 64; one more closes its
// connection, and the server goes on serving others.
TEST(CliServerTest, TooManyWaitingRequestsCloseTheConnection) {
    Served stuck("cme");
    auto server = make_server();
    server->add_endpoints("cme", {&stuck.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    std::vector<uint8_t> bytes;
    for (uint32_t id = 1; id <= 1 + 64 + 1; ++id) {
        const auto request = Client::request(id, "cme", CliCommandType::REJECT_LOGON, 1, "X");
        bytes.insert(bytes.end(), request.begin(), request.end());
    }
    ASSERT_TRUE(client.send(bytes));
    EXPECT_TRUE(client.closed());

    Client other(server->listen_port());
    auto reply = other.call("", CliCommandType::LIST_TARGETS);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, CliStatus::OK);
    server->stop();
}

// Requests sent back to back, the first split across two sends, are answered one at a
// time, in order.
TEST(CliServerTest, PipelinedAndSplitRequestsAreAnsweredInOrder) {
    Served sim("sim");
    sim.serve();
    auto server = make_server();
    server->add_endpoints("sim", {&sim.endpoint});
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());

    auto bytes = Client::request(1, "sim", CliCommandType::SET_LOG_LEVEL, 0, "a");
    const auto second = Client::request(2, "", CliCommandType::LIST_TARGETS);
    const auto third = Client::request(3, "sim", CliCommandType::SET_LOG_LEVEL, 0, "c");
    bytes.insert(bytes.end(), second.begin(), second.end());
    bytes.insert(bytes.end(), third.begin(), third.end());
    ASSERT_TRUE(client.send(std::span(bytes).first(10)));
    std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(client.send(std::span(bytes).subspan(10)));

    for (uint32_t id : {1u, 2u, 3u}) {
        auto reply = client.read();
        ASSERT_TRUE(reply);
        EXPECT_EQ(reply->request_id, id);
        EXPECT_EQ(reply->status, CliStatus::OK);
    }
    server->stop();
}

TEST(CliServerTest, AMalformedMessageClosesTheConnection) {
    auto server = make_server();
    ASSERT_TRUE(server->start());
    Client client(server->listen_port());
    ASSERT_TRUE(client.connected());
    const ControlHeader start{sizeof(ControlHeader), ControlMessageType::START};
    ASSERT_TRUE(client.send(std::span(reinterpret_cast<const uint8_t *>(&start), sizeof(start))));
    EXPECT_TRUE(client.closed());
    server->stop();
}
