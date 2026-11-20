#pragma once

#include "cli_endpoint.hpp"
#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

/// The CLI commands that switch a gateway's business rejects per ID: REJECT_LOGON,
/// REJECT_NEW_ORDERS and LIST_REJECTS. Shared by the venues that offer them.
namespace slick::sim::cli {

/// Adds the command's ID to `ids` (flag 1) or removes it (flag 0), and says so in `out`.
/// False when there is no ID. `Set` is an unordered_set<std::string> with a transparent
/// hash, as the gateways keep their lists in.
template <typename Set>
bool toggle_id(Set &ids, const Command &command, std::string &out) {
    const auto id = command.arg_view();
    if (id.empty()) {
        out = std::format("{} needs an ID", to_string(command.command));
        return false;
    }
    const bool on = command.flag != 0;
    bool changed;
    if (on) {
        changed = ids.emplace(id).second;
    } else {
        auto it = ids.find(id);
        changed = it != ids.end();
        if (changed) {
            ids.erase(it);
        }
    }
    out = std::format("{} {} {}{}", to_string(command.command), on ? "on" : "off", id, changed ? "" : " (unchanged)");
    return true;
}

/// "<name>: a b c", sorted, or "<name>: (none)".
template <typename Set>
void list_ids(const Set &ids, std::string_view name, std::string &out) {
    std::vector<std::string_view> sorted(ids.begin(), ids.end());
    std::sort(sorted.begin(), sorted.end());
    out += name;
    out += ':';
    if (sorted.empty()) {
        out += " (none)";
    }
    for (auto id : sorted) {
        out += ' ';
        out += id;
    }
}

/// Serves the reject commands against a gateway's two lists. `on_new_orders(id, on)`
/// hears every REJECT_NEW_ORDERS, so sessions already open can follow it. A logon
/// refusal needs no such call: it is checked at each logon. False, with the reason in
/// `out`, for any other command.
template <typename Set, typename OnNewOrders>
bool serve_rejects(const Command &command, Set &reject_logon, Set &reject_new_orders, OnNewOrders &&on_new_orders,
                   std::string &out) {
    switch (command.command) {
    case CliCommandType::REJECT_LOGON:
        return toggle_id(reject_logon, command, out);
    case CliCommandType::REJECT_NEW_ORDERS:
        if (!toggle_id(reject_new_orders, command, out)) {
            return false;
        }
        on_new_orders(command.arg_view(), command.flag != 0);
        return true;
    case CliCommandType::LIST_REJECTS:
        list_ids(reject_logon, to_string(CliCommandType::REJECT_LOGON), out);
        out += '\n';
        list_ids(reject_new_orders, to_string(CliCommandType::REJECT_NEW_ORDERS), out);
        return true;
    default:
        out = std::format("{} is not served here", to_string(command.command));
        return false;
    }
}

}   // namespace slick::sim::cli
