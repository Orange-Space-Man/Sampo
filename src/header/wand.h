#pragma once

#include <string>

namespace lua51 {
    struct lua_State;
}

namespace wand {
    bool init();
    bool load(lua51::lua_State* state, const char* filename, int& entity, std::string& error);
    bool save(lua51::lua_State* state, int entity, const char* filename, std::string& path, std::string& error);
    void update(lua51::lua_State* state);
    void drawMenu();
}
