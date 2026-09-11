#pragma once

namespace lua51 {
    struct lua_State;
}

namespace qol_wand_comparison {
    bool init();
    void update(lua51::lua_State* state);
}
