// lua 51 stuff

#include "noita.h"
#include "memory.h"
#include "lua51.h"
#include <cstdint>
#include "log.h"

namespace lua51
{
    struct lua_State;
    struct lua_Debug {
        int event;
        const char* name;
        const char* namewhat;
        const char* what;
        const char* source;
        int currentline;
        int nups;
        int linedefined;
        int lastlinedefined;
        char short_src[60];
        int i_ci;
    };

    using LuaCFunction = int(__cdecl*)(lua_State*);
    using LuaLNewState = lua_State * (__cdecl*)();
    using LuaLOpenLibs = void(__cdecl*)(lua_State*);
    using LuaClose = void(__cdecl*)(lua_State*);
    using LuaPCall = int(__cdecl*)(lua_State*, int, int, int);
    using LuaLLoadBufferX = int(__cdecl*)(lua_State*, const char*, size_t, const char*, const char*);
    using LuaLLoadString = int(__cdecl*)(lua_State*, const char*);
    using LuaLLoadFile = int(__cdecl*)(lua_State*, const char*);
    using LuaLLoadFileX = int(__cdecl*)(lua_State*, const char*, const char*);
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
    using LuaNext = int(__cdecl*)(lua_State*, int);
    using LuaType = int(__cdecl*)(lua_State*, int);
    using LuaToLString = const char* (__cdecl*)(lua_State*, int, size_t*);
    using LuaToCFunction = LuaCFunction(__cdecl*)(lua_State*, int);
    using LuaToPointer = const void* (__cdecl*)(lua_State*, int);
    using LuaLRef = int(__cdecl*)(lua_State*, int);
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
    LuaNext f_next = nullptr;
    LuaType f_type = nullptr;
    LuaToLString f_toLString = nullptr;
    LuaToCFunction f_toCFunction = nullptr;
    LuaToPointer f_toPointer = nullptr;
    LuaLRef f_lRef = nullptr;
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

    template <typename T>
    T LuaExport(HMODULE module, const char* name) {
        T exportedFunction = reinterpret_cast<T>(GetProcAddress(module, name));

        if (exportedFunction == nullptr) {
            sampo::log::write(" Could not find %s in lua51.dll module", name);
        } else {
            sampo::log::write("Found %s.. [%p]", name, exportedFunction);
        }

        return exportedFunction;
    }

    lua_State* __cdecl hookLNewState() {
        lua_State* const state = f_oNewState();
        InterlockedExchangePointer(&p_oState, state);
        return state;
    }

    int __cdecl hookPCall(lua_State* state, int arg, int result, int error) {
        InterlockedExchangePointer(&p_oState, state);
        return f_oPCall(state, arg, result, error);
    }

    int __cdecl hookLoadBuffer(lua_State* state, const char* buffer, size_t size, const char* name, const char* mode) {
        InterlockedExchangePointer(&p_oState, state);
        return f_oLoadBuffer(state, buffer, size, name, mode);
    }

    int __cdecl hookLoadString(lua_State* state, const char* source) {
        InterlockedExchangePointer(&p_oState, state);
        return f_oLoadString(state, source);
    }

    int __cdecl hookLoadFile(lua_State* state, const char* filename)
    {
        InterlockedExchangePointer(&p_oState, state);
        return f_oLoadFile(state, filename);
    }

    int __cdecl hookLoadFileX(lua_State* state, const char* filename, const char* mode)
    {
        InterlockedExchangePointer(&p_oState, state);
        return f_oLoadFileX(state, filename, mode);
    }

    void __cdecl hookClose(lua_State* state) {
        InterlockedCompareExchangePointer(&p_oState, nullptr, state);
        f_oClose(state);
    }

    bool lua51::init() {
        sampo::log::write("Initializing lua51 hook..");
        if (noita::lua51Base == nullptr) {
            sampo::log::write("Lua51 hook failed: could not locate lua51.dll");
            return false;
        }

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
        f_next = LuaExport<LuaNext>(noita::lua51Base, "lua_next");
        f_type = LuaExport<LuaType>(noita::lua51Base, "lua_type");
        f_toLString = LuaExport<LuaToLString>(noita::lua51Base, "lua_tolstring");
        f_toCFunction = LuaExport<LuaToCFunction>(noita::lua51Base, "lua_tocfunction");
        f_toPointer = LuaExport<LuaToPointer>(noita::lua51Base, "lua_topointer");
        f_lRef = LuaExport<LuaLRef>(noita::lua51Base, "luaL_ref");
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
            sampo::log::write("Functions hooked successfully: /arrow luaL_newstate: %p /arrow lua_close: %p /arrow lua_pcall: %p /arrow luaL_loadbufferx: %p /arrow luaL_loadstring: %p /arrow luaL_loadfile: %p /arrow luaL_loadfilex: %p ",newStateHooked, closeHooked, pcallHooked, loadBufferHooked, loadStringHooked, loadFileHooked, loadFileXHooked);
            return true;
        }

        sampo::log::write("One or more mandatory functions failed to hook: /arrow luaL_newstate: %p /arrow lua_close: %p /arrow lua_pcall: %p /arrow luaL_loadbufferx: %p /arrow luaL_loadstring: %p /arrow luaL_loadfile: %p /arrow luaL_loadfilex: %p ", newStateHooked, closeHooked, pcallHooked, loadBufferHooked, loadStringHooked, loadFileHooked, loadFileXHooked);
        return false;
    }

    // lua functions

    lua_State* GetState() {
        return static_cast<lua_State*>(InterlockedCompareExchangePointer(&p_oState, nullptr, nullptr));
    }
}