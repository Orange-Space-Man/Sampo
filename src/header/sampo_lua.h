#pragma once

namespace lua51 {
    struct lua_State;
}

namespace sampo_lua {
    bool load(lua51::lua_State* state);
}
