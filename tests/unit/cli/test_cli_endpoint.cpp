#include <gtest/gtest.h>
#include <cli/cli_endpoint.hpp>
#include <cli/reject_commands.hpp>
#include <utils/fixed_string.hpp>
#include "cli_test_util.hpp"
#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace slick::sim;
using namespace slick::sim::test;

namespace {

/// What every command tested here says back: "<command> <flag> <arg>".
bool echo(const cli::Command &command, std::string &out) {
    out = std::format("{} {} {}", to_string(command.command), static_cast<int>(command.flag), command.arg_view());
    return true;
}

/// Serves `endpoint` on its own thread, as a gateway would, until destroyed.
class Owner {
public:
    template <typename Handler>
    Owner(cli::CliEndpoint &endpoint, Handler handler)
        : thread_([this, &endpoint, handler]() mutable {
              while (run_.load(std::memory_order_relaxed)) {
                  endpoint.serve(handler);
                  std::this_thread::yield();
              }
          }) {}
    ~Owner() {
        run_ = false;
        thread_.join();
    }

private:
    std::atomic_bool run_{true};
    std::thread thread_;
};

using Ids = std::unordered_set<std::string, utils::StringViewHash, utils::StringViewEq>;

}   // namespace

TEST(CliEndpointTest, ACommandIsServedOnTheOwnerThreadAndAnswered) {
    cli::CliEndpoint endpoint("cme");
    std::thread::id served_on;
    Owner owner(endpoint, [&served_on](const cli::Command &command, std::string &out) {
        served_on = std::this_thread::get_id();
        return echo(command, out);
    });
    auto answer = cli_call(endpoint, CliCommandType::REJECT_LOGON, 1, "FIRM9:BAD");
    ASSERT_TRUE(answer);
    EXPECT_EQ(answer->status, CliStatus::OK);
    EXPECT_EQ(answer->text, "reject_logon 1 FIRM9:BAD");
    EXPECT_NE(served_on, std::this_thread::get_id());
}

TEST(CliEndpointTest, AHandlerThatFailsAnswersFailed) {
    cli::CliEndpoint endpoint;
    Owner owner(endpoint, [](const cli::Command &, std::string &out) {
        out = "no";
        return false;
    });
    auto answer = cli_call(endpoint, CliCommandType::SHUTDOWN);
    ASSERT_TRUE(answer);
    EXPECT_EQ(answer->status, CliStatus::FAILED);
    EXPECT_EQ(answer->text, "no");
}

// Commands queued before the owner gets to them are answered in order, each under its
// own ticket, by one serve().
TEST(CliEndpointTest, QueuedCommandsAreAnsweredInOrderUnderTheirTickets) {
    cli::CliEndpoint endpoint;
    for (uint64_t ticket = 1; ticket <= cli::CliEndpoint::MAX_IN_FLIGHT; ++ticket) {
        ASSERT_TRUE(
            endpoint.post(cli_command(ticket, CliCommandType::REJECT_NEW_ORDERS, 0, std::format("ID{}", ticket))));
    }
    endpoint.serve(echo);
    uint64_t expected = 1;
    endpoint.take_replies([&](uint64_t ticket, CliStatus status, std::string_view text) {
        EXPECT_EQ(ticket, expected);
        EXPECT_EQ(status, CliStatus::OK);
        EXPECT_EQ(text, std::format("reject_new_orders 0 ID{}", expected));
        ++expected;
    });
    EXPECT_EQ(expected, cli::CliEndpoint::MAX_IN_FLIGHT + 1);
}

// The inbox holds MAX_IN_FLIGHT. post() refuses one more rather than lap it, and a
// command keeps its slot until its reply is taken, served or not.
TEST(CliEndpointTest, PostRefusesWhileFullUntilTheRepliesAreTaken) {
    cli::CliEndpoint endpoint;
    for (uint64_t ticket = 1; ticket <= cli::CliEndpoint::MAX_IN_FLIGHT; ++ticket) {
        ASSERT_TRUE(endpoint.post(cli_command(ticket, CliCommandType::LIST_REJECTS)));
    }
    EXPECT_TRUE(endpoint.busy());
    EXPECT_FALSE(endpoint.post(cli_command(99, CliCommandType::LIST_REJECTS)));
    endpoint.serve(echo);
    EXPECT_TRUE(endpoint.busy());

    std::vector<uint64_t> tickets;
    endpoint.take_replies([&](uint64_t ticket, CliStatus, std::string_view) { tickets.push_back(ticket); });
    ASSERT_EQ(tickets.size(), cli::CliEndpoint::MAX_IN_FLIGHT);
    EXPECT_EQ(tickets.back(), cli::CliEndpoint::MAX_IN_FLIGHT);
    EXPECT_FALSE(endpoint.busy());
    EXPECT_TRUE(endpoint.post(cli_command(100, CliCommandType::LIST_REJECTS)));
}

TEST(CliEndpointTest, ALongReplyIsCut) {
    cli::CliEndpoint endpoint;
    ASSERT_TRUE(endpoint.post(cli_command(7, CliCommandType::LIST_REJECTS)));
    endpoint.serve([](const cli::Command &, std::string &out) {
        out.assign(3 * cli::CliEndpoint::MAX_REPLY_TEXT, 'x');
        return true;
    });
    std::string text;
    endpoint.take_replies([&](uint64_t, CliStatus, std::string_view reply) { text = reply; });
    ASSERT_EQ(text.size(), cli::CliEndpoint::MAX_REPLY_TEXT);
    EXPECT_TRUE(text.ends_with("x..."));
}

TEST(CliEndpointTest, AnArgFillingTheFieldNeedsNoTerminator) {
    const std::string arg(sizeof(cli::Command::arg), 'a');
    EXPECT_EQ(cli_command(1, CliCommandType::REJECT_LOGON, 1, arg).arg_view(), arg);
}

TEST(CliEndpointTest, InstancePathLeavesOutAVenuesLoneInstance) {
    EXPECT_EQ(cli::instance_path("cme", "cme"), "cme");
    EXPECT_EQ(cli::instance_path("cme", "equity"), "cme/equity");
}

TEST(CliRejectCommandsTest, AnIdIsSwitchedOnAndOff) {
    Ids ids;
    std::string out;
    EXPECT_TRUE(cli::toggle_id(ids, cli_command(1, CliCommandType::REJECT_LOGON, 1, "A"), out));
    EXPECT_EQ(out, "reject_logon on A");
    EXPECT_TRUE(ids.contains("A"));
    EXPECT_TRUE(cli::toggle_id(ids, cli_command(1, CliCommandType::REJECT_LOGON, 1, "A"), out));
    EXPECT_EQ(out, "reject_logon on A (unchanged)");
    EXPECT_TRUE(cli::toggle_id(ids, cli_command(1, CliCommandType::REJECT_LOGON, 0, "A"), out));
    EXPECT_EQ(out, "reject_logon off A");
    EXPECT_TRUE(ids.empty());
    EXPECT_TRUE(cli::toggle_id(ids, cli_command(1, CliCommandType::REJECT_LOGON, 0, "A"), out));
    EXPECT_EQ(out, "reject_logon off A (unchanged)");
    EXPECT_FALSE(cli::toggle_id(ids, cli_command(1, CliCommandType::REJECT_LOGON, 1, ""), out));
    EXPECT_TRUE(ids.empty());
}

// Each REJECT_NEW_ORDERS reaches the gateway's hook, so open sessions follow it. The
// listing is sorted. Anything else is not a reject command.
TEST(CliRejectCommandsTest, ServesBothListsAndTellsTheGatewayOfNewOrderChanges) {
    Ids logon{"B", "A"};
    Ids new_orders;
    std::vector<std::pair<std::string, bool>> heard;
    auto hook = [&heard](std::string_view id, bool on) { heard.emplace_back(id, on); };
    std::string out;

    EXPECT_TRUE(cli::serve_rejects(cli_command(1, CliCommandType::REJECT_NEW_ORDERS, 1, "C"), logon, new_orders,
                                   hook, out));
    EXPECT_TRUE(cli::serve_rejects(cli_command(2, CliCommandType::REJECT_LOGON, 0, "B"), logon, new_orders, hook,
                                   out));
    EXPECT_EQ(heard, (std::vector<std::pair<std::string, bool>>{{"C", true}}));

    out.clear();
    EXPECT_TRUE(cli::serve_rejects(cli_command(3, CliCommandType::LIST_REJECTS), logon, new_orders, hook, out));
    EXPECT_EQ(out, "reject_logon: A\nreject_new_orders: C");

    out.clear();
    EXPECT_TRUE(cli::serve_rejects(cli_command(4, CliCommandType::REJECT_NEW_ORDERS, 0, "C"), logon, new_orders,
                                   hook, out));
    EXPECT_EQ(heard.back(), (std::pair<std::string, bool>{"C", false}));
    out.clear();
    EXPECT_TRUE(cli::serve_rejects(cli_command(5, CliCommandType::LIST_REJECTS), logon, new_orders, hook, out));
    EXPECT_EQ(out, "reject_logon: A\nreject_new_orders: (none)");

    EXPECT_FALSE(cli::serve_rejects(cli_command(6, CliCommandType::SHUTDOWN), logon, new_orders, hook, out));
    EXPECT_EQ(out, "shutdown is not served here");
}
