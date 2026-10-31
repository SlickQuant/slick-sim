#pragma once

#include <nlohmann/json.hpp>
#include <slick/logger.hpp>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// Helpers for a venue whose one config entry describes several independent
/// exchange instances, such as CME's market segments or ICE's silos. Each instance
/// becomes its own Exchange, on its own thread.
namespace slick::sim::exch {

/// Expands a venue's config entry into one config per instance listed under
/// `list_key`.
///
/// Without `list_key`, the entry is the only instance. With it, each instance
/// starts from the entry's other keys, which all instances share, merge-patched
/// with the instance's own. RFC 7386 merges objects key by key and replaces
/// anything else, arrays included. An instance with `"enabled": false` is skipped.
///
/// An instance without a `name` gets `default_name(instance, index)`; the single
/// entry without `list_key` gets `venue`.
template <typename DefaultName>
std::vector<nlohmann::json> expand_instances(const nlohmann::json &config, std::string_view list_key,
                                             std::string_view venue, DefaultName &&default_name) {
    std::vector<nlohmann::json> out;
    const std::string key(list_key);
    if (!config.contains(key)) {
        auto &single = out.emplace_back(config);
        if (!single.contains("name")) {
            single["name"] = std::string(venue);
        }
        return out;
    }

    const auto &list = config[key];
    if (!list.is_array() || list.empty()) {
        throw std::runtime_error(std::format("{}: \"{}\" must be a non-empty array", venue, key));
    }
    nlohmann::json shared = config;
    shared.erase(key);
    for (size_t i = 0; i < list.size(); ++i) {
        // RFC 7386 treats a non-object patch as a replacement, so a typo here would
        // silently drop every shared key - or, for a string, throw from nlohmann a long
        // way from the config line that caused it.
        if (!list[i].is_object()) {
            throw std::runtime_error(std::format("{}: \"{}\"[{}] must be an object", venue, key, i));
        }
        nlohmann::json instance = shared;
        instance.merge_patch(list[i]);
        if (!instance.contains("name")) {
            instance["name"] = default_name(instance, i);
        }
        if (!instance.value("enabled", true)) {
            LOG_INFO("{} {} is disabled, skipping", venue, instance["name"].get<std::string>());
            continue;
        }
        out.push_back(std::move(instance));
    }
    return out;
}

/// Records that instance `owner` uses `value`. Throws if another instance already
/// does, or if this one claims it twice. Instances run side by side in one
/// process, so an instrument, port or multicast group can belong to one only.
template <typename T>
void claim(std::unordered_map<T, std::string> &claimed, const T &value, const std::string &owner,
           std::string_view what, std::string_view venue) {
    auto [it, inserted] = claimed.emplace(value, owner);
    if (inserted) {
        return;
    }
    if (it->second == owner) {
        throw std::runtime_error(std::format("{}: '{}' uses {} {} twice", venue, owner, what, value));
    }
    throw std::runtime_error(std::format("{}: '{}' and '{}' both use {} {}", venue, it->second, owner, what, value));
}

/// "address:port" of a `{ "address", "port" }` multicast group config, as claimed.
inline std::string group_key(const nlohmann::json &group) {
    return std::format("{}:{}", group.at("address").get<std::string>(), group.at("port").get<uint16_t>());
}

}   // namespace slick::sim::exch
