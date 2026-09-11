// lua 51 stuff

#include "noita.h"
#include "memory.h"
#include "lua51.h"
#include "sampo_lua.h"
#include "qol_spell_descriptions.h"
#include "qol_wand_comparison.h"
#include "mod_settings.h"
#include "wand.h"
#include <cstdint>
#include "log.h"

namespace lua51
{
    using LuaCFunction = int(__cdecl*)(lua_State*);
    using LuaLNewState = lua_State * (__cdecl*)();
    using LuaLOpenLibs = void(__cdecl*)(lua_State*);
    using LuaClose = void(__cdecl*)(lua_State*);
    using LuaPCall = int(__cdecl*)(lua_State*, int, int, int);
    using LuaLLoadBufferX = int(__cdecl*)(lua_State*, const char*, size_t, const char*, const char*);
    using LuaLLoadString = int(__cdecl*)(lua_State*, const char*);
    using LuaLLoadFile = int(__cdecl*)(lua_State*, const char*);
    using LuaLLoadFileX = int(__cdecl*)(lua_State*, const char*, const char*);
    using LuaCreateTable = void(__cdecl*)(lua_State*, int, int);
    using LuaLError = int(__cdecl*)(lua_State*, const char*, ...);
    using LuaGetTop = int(__cdecl*)(lua_State*);
    using LuaSetTop = void(__cdecl*)(lua_State*, int);
    using LuaPushNil = void(__cdecl*)(lua_State*);
    using LuaPushString = void(__cdecl*)(lua_State*, const char*);
    using LuaPushNumber = void(__cdecl*)(lua_State*, double);
    using LuaPushBoolean = void(__cdecl*)(lua_State*, int);
    using LuaPushCClosure = void(__cdecl*)(lua_State*, LuaCFunction, int);
    using LuaPushValue = void(__cdecl*)(lua_State*, int);
    using LuaGetField = void(__cdecl*)(lua_State*, int, const char*);
    using LuaSetField = void(__cdecl*)(lua_State*, int, const char*);
    using LuaRawGetIndex = void(__cdecl*)(lua_State*, int, int);
    using LuaRawSetIndex = void(__cdecl*)(lua_State*, int, int);
    using LuaNext = int(__cdecl*)(lua_State*, int);
    using LuaType = int(__cdecl*)(lua_State*, int);
    using LuaToLString = const char* (__cdecl*)(lua_State*, int, size_t*);
    using LuaToCFunction = LuaCFunction(__cdecl*)(lua_State*, int);
    using LuaToPointer = const void* (__cdecl*)(lua_State*, int);
    using LuaLRef = int(__cdecl*)(lua_State*, int);
    using LuaLUnref = void(__cdecl*)(lua_State*, int, int);
    using LuaHook = void(__cdecl*)(lua_State*, lua_Debug*);
    using LuaSetHook = int(__cdecl*)(lua_State*, LuaHook, int, int);
    using LuaGetHook = LuaHook(__cdecl*)(lua_State*);
    using LuaGetHookMask = int(__cdecl*)(lua_State*);
    using LuaGetHookCount = int(__cdecl*)(lua_State*);
    using LuaGetInfo = int(__cdecl*)(lua_State*, const char*, lua_Debug*);
    using LuaGetStack = int(__cdecl*)(lua_State*, int, lua_Debug*);
    using LuaGetLocal = const char* (__cdecl*)(lua_State*, const lua_Debug*, int);
    using LuaGetUpvalue = const char* (__cdecl*)(lua_State*, int, int);
    using LuaToBoolean = int(__cdecl*)(lua_State*, int);
    using LuaToNumber = double(__cdecl*)(lua_State*, int);
    using LuaTypeName = const char* (__cdecl*)(lua_State*, int);

    constexpr int lRegistryIndex = -10000;
    constexpr int lGlobalsIndex = -10002;

    void* volatile p_oState = nullptr;
    LuaLNewState f_oNewState = nullptr;
    LuaLOpenLibs f_openLibs = nullptr;
    LuaClose f_oClose = nullptr;
    LuaPCall f_oPCall = nullptr;
    LuaLLoadBufferX f_oLoadBuffer = nullptr;
    LuaLLoadString f_oLoadString = nullptr;
    LuaLLoadFile f_oLoadFile = nullptr;
    LuaLLoadFileX f_oLoadFileX = nullptr;
    LuaPCall f_pCall = nullptr;

    LuaCreateTable f_createTable = nullptr;
    LuaLError f_lError = nullptr;
    LuaGetTop f_getTop = nullptr;
    LuaSetTop f_setTop = nullptr;
    LuaPushNil f_pushNil = nullptr;
    LuaPushString f_pushString = nullptr;
    LuaPushNumber f_pushNumber = nullptr;
    LuaPushBoolean f_pushBoolean = nullptr;
    LuaPushCClosure f_pushCClosure = nullptr;
    LuaPushValue f_pushValue = nullptr;
    LuaGetField f_getField = nullptr;
    LuaSetField f_setField = nullptr;
    LuaRawGetIndex f_rawGetIndex = nullptr;
    LuaRawSetIndex f_rawSetIndex = nullptr;
    LuaNext f_next = nullptr;
    LuaType f_type = nullptr;
    LuaToLString f_toLString = nullptr;
    LuaToCFunction f_toCFunction = nullptr;
    LuaToPointer f_toPointer = nullptr;
    LuaLRef f_lRef = nullptr;
    LuaLUnref f_lUnref = nullptr;
    LuaSetHook f_setHook = nullptr;
    LuaGetHook f_getHook = nullptr;
    LuaGetHookMask f_getHookMask = nullptr;
    LuaGetHookCount f_getHookCount = nullptr;
    LuaGetInfo f_getInfo = nullptr;
    LuaGetStack f_getStack = nullptr;
    LuaGetLocal f_getLocal = nullptr;
    LuaGetUpvalue f_getUpvalue = nullptr;
    LuaToBoolean f_toBoolean = nullptr;
    LuaToNumber f_toNumber = nullptr;
    LuaTypeName f_typeName = nullptr;

    const char* boolText(bool value) {
        if (value) {
            return "true";
        }
        return "false";
    }

    template <typename T>
    T LuaExport(HMODULE module, const char* name) {
        T exportedFunction = reinterpret_cast<T>(GetProcAddress(module, name));

        if (exportedFunction == nullptr) {
            sampo::log::error("Could not find %s in module", name);
        } else {
            sampo::log::write("Found %s.. [%p]", name, reinterpret_cast<void*>(exportedFunction));
        }

        return exportedFunction;
    }

    lua_State* __cdecl hookLNewState() {
        lua_State* const state = f_oNewState();
        InterlockedExchangePointer(&p_oState, state);
        sampo_lua::load(state);
        return state;
    }

    int __cdecl hookPCall(lua_State* state, int arg, int result, int error) {
        InterlockedExchangePointer(&p_oState, state);
        sampo_lua::load(state);
        const int status = f_oPCall(state, arg, result, error);
        spell_descriptions::update(state);
        qol_wand_comparison::update(state);
        wand::update(state);
        return status;
    }

    int __cdecl hookLoadBuffer(lua_State* state, const char* buffer, size_t size, const char* name, const char* mode) {
        InterlockedExchangePointer(&p_oState, state);
        sampo_lua::load(state);
        std::string filteredSource;
        if (mod_settings::filterSource(name, buffer, size, filteredSource)) {
            return f_oLoadBuffer(state, filteredSource.data(), filteredSource.size(), name, mode);
        }
        return f_oLoadBuffer(state, buffer, size, name, mode);
    }

    int __cdecl hookLoadString(lua_State* state, const char* source) {
        InterlockedExchangePointer(&p_oState, state);
        sampo_lua::load(state);
        return f_oLoadString(state, source);
    }

    int __cdecl hookLoadFile(lua_State* state, const char* filename)
    {
        InterlockedExchangePointer(&p_oState, state);
        sampo_lua::load(state);
        return f_oLoadFile(state, filename);
    }

    int __cdecl hookLoadFileX(lua_State* state, const char* filename, const char* mode)
    {
        InterlockedExchangePointer(&p_oState, state);
        sampo_lua::load(state);
        return f_oLoadFileX(state, filename, mode);
    }

    void __cdecl hookClose(lua_State* state) {
        InterlockedCompareExchangePointer(&p_oState, nullptr, state);
        f_oClose(state);
    }

    bool lua51::init() {
        sampo::log::write("Initializing lua51 hook..");
        if (noita::lua51Base == nullptr) {
            sampo::log::error("Lua51 hook failed: could not locate lua51.dll");
            return false;
        }

        f_createTable = LuaExport<LuaCreateTable>(noita::lua51Base, "lua_createtable");
        f_lError = LuaExport<LuaLError>(noita::lua51Base, "luaL_error");
        f_pCall = LuaExport<LuaPCall>(noita::lua51Base, "lua_pcall");
        f_openLibs = LuaExport<LuaLOpenLibs>(noita::lua51Base, "luaL_openlibs");
        f_getTop = LuaExport<LuaGetTop>(noita::lua51Base, "lua_gettop");
        f_setTop = LuaExport<LuaSetTop>(noita::lua51Base, "lua_settop");
        f_pushNil = LuaExport<LuaPushNil>(noita::lua51Base, "lua_pushnil");
        f_pushString = LuaExport<LuaPushString>(noita::lua51Base, "lua_pushstring");
        f_pushNumber = LuaExport<LuaPushNumber>(noita::lua51Base, "lua_pushnumber");
        f_pushBoolean = LuaExport<LuaPushBoolean>(noita::lua51Base, "lua_pushboolean");
        f_pushCClosure = LuaExport<LuaPushCClosure>(noita::lua51Base, "lua_pushcclosure");
        f_pushValue = LuaExport<LuaPushValue>(noita::lua51Base, "lua_pushvalue");
        f_getField = LuaExport<LuaGetField>(noita::lua51Base, "lua_getfield");
        f_setField = LuaExport<LuaSetField>(noita::lua51Base, "lua_setfield");
        f_rawGetIndex = LuaExport<LuaRawGetIndex>(noita::lua51Base, "lua_rawgeti");
        f_rawSetIndex = LuaExport<LuaRawSetIndex>(noita::lua51Base, "lua_rawseti");
        f_next = LuaExport<LuaNext>(noita::lua51Base, "lua_next");
        f_type = LuaExport<LuaType>(noita::lua51Base, "lua_type");
        f_toLString = LuaExport<LuaToLString>(noita::lua51Base, "lua_tolstring");
        f_toCFunction = LuaExport<LuaToCFunction>(noita::lua51Base, "lua_tocfunction");
        f_toPointer = LuaExport<LuaToPointer>(noita::lua51Base, "lua_topointer");
        f_lRef = LuaExport<LuaLRef>(noita::lua51Base, "luaL_ref");
        f_lUnref = LuaExport<LuaLUnref>(noita::lua51Base, "luaL_unref");
        f_setHook = LuaExport<LuaSetHook>(noita::lua51Base, "lua_sethook");
        f_getHook = LuaExport<LuaGetHook>(noita::lua51Base, "lua_gethook");
        f_getHookMask = LuaExport<LuaGetHookMask>(noita::lua51Base, "lua_gethookmask");
        f_getHookCount = LuaExport<LuaGetHookCount>(noita::lua51Base, "lua_gethookcount");
        f_getInfo = LuaExport<LuaGetInfo>(noita::lua51Base, "lua_getinfo");
        f_getStack = LuaExport<LuaGetStack>(noita::lua51Base, "lua_getstack");
        f_getLocal = LuaExport<LuaGetLocal>(noita::lua51Base, "lua_getlocal");
        f_getUpvalue = LuaExport<LuaGetUpvalue>(noita::lua51Base, "lua_getupvalue");
        f_toBoolean = LuaExport<LuaToBoolean>(noita::lua51Base, "lua_toboolean");
        f_toNumber = LuaExport<LuaToNumber>(noita::lua51Base, "lua_tonumber");
        f_typeName = LuaExport<LuaTypeName>(noita::lua51Base, "lua_typename");

        sampo::log::write("Hooking lua functions");
        const bool newStateHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "luaL_newstate", reinterpret_cast<void*>(&hookLNewState), reinterpret_cast<void**>(&f_oNewState));
        const bool closeHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "lua_close", reinterpret_cast<void*>(&hookClose), reinterpret_cast<void**>(&f_oClose));
        const bool pcallHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "lua_pcall", reinterpret_cast<void*>(&hookPCall), reinterpret_cast<void**>(&f_oPCall));
        const bool loadBufferHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "luaL_loadbufferx", reinterpret_cast<void*>(&hookLoadBuffer), reinterpret_cast<void**>(&f_oLoadBuffer));
        const bool loadStringHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "luaL_loadstring", reinterpret_cast<void*>(&hookLoadString), reinterpret_cast<void**>(&f_oLoadString));
        const bool loadFileHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "luaL_loadfile", reinterpret_cast<void*>(&hookLoadFile), reinterpret_cast<void**>(&f_oLoadFile));
        const bool loadFileXHooked = memory::hook_iat(noita::noitaBase, "lua51.dll", "luaL_loadfilex", reinterpret_cast<void*>(&hookLoadFileX), reinterpret_cast<void**>(&f_oLoadFileX));

        if (newStateHooked && closeHooked && pcallHooked && loadBufferHooked && loadStringHooked) {
            sampo::log::write("Functions hooked successfully: /arrow luaL_newstate: %s /arrow lua_close: %s /arrow lua_pcall: %s /arrow luaL_loadbufferx: %s /arrow luaL_loadstring: %s /arrow luaL_loadfile: %s /arrow luaL_loadfilex: %s", boolText(newStateHooked), boolText(closeHooked), boolText(pcallHooked), boolText(loadBufferHooked), boolText(loadStringHooked), boolText(loadFileHooked), boolText(loadFileXHooked));
            return true;
        }

        sampo::log::error("One or more mandatory functions failed to hook: /arrow luaL_newstate: %s /arrow lua_close: %s /arrow lua_pcall: %s /arrow luaL_loadbufferx: %s /arrow luaL_loadstring: %s /arrow luaL_loadfile: %s /arrow luaL_loadfilex: %s", boolText(newStateHooked), boolText(closeHooked), boolText(pcallHooked), boolText(loadBufferHooked), boolText(loadStringHooked), boolText(loadFileHooked), boolText(loadFileXHooked));
        return false;
    }

    // lua functions

    lua_State* getState() {
        return static_cast<lua_State*>(InterlockedCompareExchangePointer(&p_oState, nullptr, nullptr));
    }

    bool ready() {
        return f_createTable != nullptr && f_lError != nullptr && f_pCall != nullptr && f_getTop != nullptr && f_setTop != nullptr && f_pushNil != nullptr && f_pushString != nullptr && f_pushNumber != nullptr && f_pushBoolean != nullptr && f_pushCClosure != nullptr && f_pushValue != nullptr && f_getField != nullptr && f_setField != nullptr && f_rawGetIndex != nullptr && f_rawSetIndex != nullptr && f_type != nullptr && f_toLString != nullptr && f_toCFunction != nullptr && f_lRef != nullptr && f_lUnref != nullptr && f_toBoolean != nullptr && f_toNumber != nullptr;
    }

    int getTop(lua_State* state) {
        return f_getTop(state);
    }

    void setTop(lua_State* state, int index) {
        f_setTop(state, index);
    }

    void pop(lua_State* state, int count) {
        f_setTop(state, -count - 1);
    }

    void createTable(lua_State* state, int arrayCount, int fieldCount) {
        f_createTable(state, arrayCount, fieldCount);
    }

    void getField(lua_State* state, int index, const char* name) {
        f_getField(state, index, name);
    }

    void setField(lua_State* state, int index, const char* name) {
        f_setField(state, index, name);
    }

    void rawGetIndex(lua_State* state, int index, int item) {
        f_rawGetIndex(state, index, item);
    }

    void rawSetIndex(lua_State* state, int index, int item) {
        f_rawSetIndex(state, index, item);
    }

    int reference(lua_State* state) {
        return f_lRef(state, registryIndex);
    }

    void unreference(lua_State* state, int reference) {
        if (reference < 0 || f_lUnref == nullptr) {
            return;
        }
        f_lUnref(state, registryIndex, reference);
    }

    void getGlobal(lua_State* state, const char* name) {
        f_getField(state, globalsIndex, name);
    }

    void setGlobal(lua_State* state, const char* name) {
        f_setField(state, globalsIndex, name);
    }

    void pushNil(lua_State* state) {
        f_pushNil(state);
    }

    void pushString(lua_State* state, const char* value) {
        f_pushString(state, value);
    }

    void pushNumber(lua_State* state, double value) {
        f_pushNumber(state, value);
    }

    void pushBoolean(lua_State* state, bool value) {
        f_pushBoolean(state, value ? 1 : 0);
    }

    void pushFunction(lua_State* state, LuaCFunction function) {
        f_pushCClosure(state, function, 0);
    }

    void pushValue(lua_State* state, int index) {
        f_pushValue(state, index);
    }

    int type(lua_State* state, int index) {
        return f_type(state, index);
    }

    const char* toString(lua_State* state, int index, std::size_t* length) {
        return f_toLString(state, index, length);
    }

    double toNumber(lua_State* state, int index) {
        return f_toNumber(state, index);
    }

    bool toBoolean(lua_State* state, int index) {
        return f_toBoolean(state, index) != 0;
    }

    LuaCFunction toFunction(lua_State* state, int index) {
        return f_toCFunction(state, index);
    }

    int pcall(lua_State* state, int arguments, int results, int errorFunction) {
        return f_pCall(state, arguments, results, errorFunction);
    }

    int fail(lua_State* state, const char* message) {
        return f_lError(state, "%s", message);
    }

    int loadBuffer(lua_State* state, const char* source, std::size_t size, const char* name, const char* mode) {
        if (f_oLoadBuffer == nullptr) {
            return -1;
        }
        return f_oLoadBuffer(state, source, size, name, mode);
    }

    int loadString(lua_State* state, const char* source) {
        if (f_oLoadString == nullptr) {
            return -1;
        }
        return f_oLoadString(state, source);
    }

    int loadFile(lua_State* state, const char* filename) {
        if (f_oLoadFile == nullptr) {
            return -1;
        }
        return f_oLoadFile(state, filename);
    }

    bool getStack(lua_State* state, int level, lua_Debug* debug) {
        if (f_getStack == nullptr) {
            return false;
        }
        return f_getStack(state, level, debug) != 0;
    }

    bool getInfo(lua_State* state, const char* information, lua_Debug* debug) {
        if (f_getInfo == nullptr) {
            return false;
        }
        return f_getInfo(state, information, debug) != 0;
    }
}
