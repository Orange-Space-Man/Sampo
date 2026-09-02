#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sampo::log {
    struct Line {
        std::string text;
        bool error = false;
    };

    namespace detail {
        void add(bool error, const std::string& message);
    }

    void write(const char* format, ...);
    void error(const char* format, ...);

    std::vector<Line> snapshot();
    std::string text();
    std::uint64_t revision();
}
