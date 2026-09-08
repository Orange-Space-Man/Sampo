#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mod_settings {
    struct Mod {
        std::string id;
        std::string directory;
    };

    void setMods(const std::vector<Mod>& mods);
    bool filterSource(const char* name, const char* source, std::size_t size, std::string& filteredSource);
    void open(const std::string& id, const std::string& name);
    void draw();
}
