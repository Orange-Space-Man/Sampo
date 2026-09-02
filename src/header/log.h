#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sampo::log {
    void write(const char* format, ...);
    std::vector<std::string> snapshot();
    std::string text();
    std::uint64_t revision();
}
