#include "qol_wand_comparison.h"

#include "log.h"
#include "lua51.h"
#include "memory.h"
#include "noita.h"
#include "settings.h"

#include <cstdint>
#include <vector>

namespace {
    using EntityGet = void*(__thiscall*)(void*, int);

    constexpr ULONGLONG p_updateDelay = 16;

    std::uintptr_t p_wandDraw = 0;
    EntityGet p_entityGet = nullptr;
    volatile LONG p_heldWand = 0;
    ULONGLONG p_lastUpdate = 0;

    bool beginCall(lua51::lua_State* state, const char* function, int top) {
        lua51::getGlobal(state, function);
        if (lua51::type(state, -1) == lua51::typeFunction) {
            return true;
        }
        lua51::setTop(state, top);
        return false;
    }

    bool finishCall(lua51::lua_State* state, int arguments, int results, int top) {
        if (lua51::pcall(state, arguments, results, 0) == 0) {
            return true;
        }
        lua51::setTop(state, top);
        return false;
    }

    bool getEntities(lua51::lua_State* state, const char* tag, std::vector<int>& entities) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityGetWithTag", top)) {
            return false;
        }
        lua51::pushString(state, tag);
        if (!finishCall(state, 1, 1, top)) {
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeTable) {
            lua51::setTop(state, top);
            return true;
        }

        const int table = lua51::getTop(state);
        for (int index = 1;; index++) {
            lua51::rawGetIndex(state, table, index);
            if (lua51::type(state, -1) == lua51::typeNil) {
                lua51::pop(state, 1);
                break;
            }
            if (lua51::type(state, -1) == lua51::typeNumber) {
                entities.push_back(static_cast<int>(lua51::toNumber(state, -1)));
            }
            lua51::pop(state, 1);
        }
        lua51::setTop(state, top);
        return true;
    }

    bool getFirstComponent(lua51::lua_State* state, int entity, const char* type, int& component) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityGetFirstComponentIncludingDisabled", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(entity));
        lua51::pushString(state, type);
        if (!finishCall(state, 2, 1, top)) {
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }
        component = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);
        return true;
    }

    bool getNumber(lua51::lua_State* state, int component, const char* field, int& value) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentGetValue2", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (!finishCall(state, 2, 1, top)) {
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }
        value = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);
        return true;
    }

    bool getPlayer(lua51::lua_State* state, int& player) {
        std::vector<int> entities;
        if (!getEntities(state, "player_unit", entities)) {
            return false;
        }
        if (entities.empty() && !getEntities(state, "polymorphed_player", entities)) {
            return false;
        }
        if (entities.empty()) {
            return false;
        }
        player = entities.front();
        return true;
    }

    bool getHeldWand(lua51::lua_State* state, int player, int& wand) {
        int inventory = 0;
        if (!getFirstComponent(state, player, "Inventory2Component", inventory)) {
            return false;
        }
        if (!getNumber(state, inventory, "mActiveItem", wand) || wand == 0) {
            return false;
        }

        int ability = 0;
        return getFirstComponent(state, wand, "AbilityComponent", ability);
    }

    std::uintptr_t getWand() {
        if (!settings::wandComparison()) {
            return 0;
        }

        const LONG wand = InterlockedCompareExchange(&p_heldWand, 0, 0);
        if (wand == 0 || p_entityGet == nullptr) {
            return 0;
        }

        void* const manager = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(noita::noitaBase) + noita::entityManagerRva);
        if (manager == nullptr) {
            return 0;
        }

        return reinterpret_cast<std::uintptr_t>(p_entityGet(manager, wand));
    }

    __declspec(naked) void drawWand() {
        __asm {
            pushfd
            pushad
            call getWand
            test eax, eax
            je done
            cmp eax, dword ptr [esp + 0x28]
            je done
            mov dword ptr [esp + 0x4C], eax
        done:
            popad
            popfd
            jmp dword ptr [p_wandDraw]
        }
    }
}

bool qol_wand_comparison::init() {
    if (p_wandDraw != 0) {
        return true;
    }

    p_wandDraw = reinterpret_cast<std::uintptr_t>(noita::noitaBase) + noita::wandDrawRva;
    p_entityGet = reinterpret_cast<EntityGet>(reinterpret_cast<std::uintptr_t>(noita::noitaBase) + noita::entityGetRva);

    void* const call = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(noita::noitaBase) + noita::wandHoverCallRva);
    if (!memory::write_call(call, reinterpret_cast<const void*>(&drawWand))) {
        p_wandDraw = 0;
        p_entityGet = nullptr;
        sampo::log::error("Failed to patch Noita's wand renderer.");
        return false;
    }

    return true;
}

void qol_wand_comparison::update(lua51::lua_State* state) {
    if (!settings::wandComparison()) {
        InterlockedExchange(&p_heldWand, 0);
        return;
    }
    if (state == nullptr || !lua51::ready()) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (now - p_lastUpdate < p_updateDelay) {
        return;
    }
    p_lastUpdate = now;

    int player = 0;
    int wand = 0;
    if (!getPlayer(state, player) || !getHeldWand(state, player, wand)) {
        InterlockedExchange(&p_heldWand, 0);
        return;
    }

    InterlockedExchange(&p_heldWand, wand);
}
