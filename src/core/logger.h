#pragma once

#include <spdlog/spdlog.h>
#include <string_view>

namespace es {

inline void info(std::string_view msg) {
    spdlog::info("{}", msg);
}

}  // namespace es
