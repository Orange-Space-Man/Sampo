#include "sampo_lua.h"

#include "lua51.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace {
    std::mutex p_keyMutex;
    std::unordered_map<std::string, double> p_keyCodes;
    bool p_keyCodesLoaded = false;

    std::string upper(std::string value) {
        for (char& character : value) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return value;
    }

    std::string lower(std::string value) {
        for (char& character : value) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return value;
    }

    bool readKeyFile(lua51::lua_State* state, std::string& content) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ModTextFileGetContent");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushString(state, "data/scripts/debug/keycodes.lua");
        if (lua51::pcall(state, 1, 1, 0) != 0 || lua51::type(state, -1) != lua51::typeString) {
            lua51::setTop(state, top);
            return false;
        }

        std::size_t length = 0;
        const char* const text = lua51::toString(state, -1, &length);
        if (text != nullptr) {
            content.assign(text, length);
        }
        lua51::setTop(state, top);
        return !content.empty();
    }

    void parseKeyCodes(const std::string& content, std::unordered_map<std::string, double>& keyCodes) {
        std::size_t position = 0;
        while (true) {
            position = content.find("Key_", position);
            if (position == std::string::npos) {
                return;
            }

            const std::size_t nameStart = position + 4;
            std::size_t nameEnd = nameStart;
            while (nameEnd < content.size()) {
                const unsigned char character = static_cast<unsigned char>(content[nameEnd]);
                if (std::isalnum(character) == 0 && character != '_') {
                    break;
                }
                ++nameEnd;
            }

            std::size_t valueStart = nameEnd;
            while (valueStart < content.size() && std::isspace(static_cast<unsigned char>(content[valueStart])) != 0) {
                ++valueStart;
            }
            if (valueStart >= content.size() || content[valueStart] != '=') {
                position = nameEnd;
                continue;
            }
            ++valueStart;
            while (valueStart < content.size() && std::isspace(static_cast<unsigned char>(content[valueStart])) != 0) {
                ++valueStart;
            }

            char* valueEnd = nullptr;
            const unsigned long value = std::strtoul(content.c_str() + valueStart, &valueEnd, 10);
            if (nameEnd > nameStart && valueEnd != content.c_str() + valueStart) {
                keyCodes[upper(content.substr(nameStart, nameEnd - nameStart))] = static_cast<double>(value);
            }
            position = nameEnd;
        }
    }

    bool fileKeyCode(lua51::lua_State* state, const std::string& label, double& keyCode) {
        {
            std::scoped_lock lock(p_keyMutex);
            if (p_keyCodesLoaded) {
                const auto found = p_keyCodes.find(label);
                if (found == p_keyCodes.end()) {
                    return false;
                }
                keyCode = found->second;
                return true;
            }
        }

        std::string content;
        if (!readKeyFile(state, content)) {
            return false;
        }

        std::unordered_map<std::string, double> keyCodes;
        parseKeyCodes(content, keyCodes);
        if (keyCodes.empty()) {
            return false;
        }

        std::scoped_lock lock(p_keyMutex);
        p_keyCodes = std::move(keyCodes);
        p_keyCodesLoaded = true;
        const auto found = p_keyCodes.find(label);
        if (found == p_keyCodes.end()) {
            return false;
        }
        keyCode = found->second;
        return true;
    }

    bool globalKeyCode(lua51::lua_State* state, const std::string& name, double& keyCode) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, name.c_str());
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }

        keyCode = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool findKeyCode(lua51::lua_State* state, const std::string& label, double& keyCode) {
        if (fileKeyCode(state, label, keyCode)) {
            return true;
        }
        if (globalKeyCode(state, "Key_" + label, keyCode)) {
            return true;
        }
        if (globalKeyCode(state, "Key_" + lower(label), keyCode)) {
            return true;
        }
        return globalKeyCode(state, "KEY_" + label, keyCode);
    }

    int callInput(lua51::lua_State* state, const char* function, double value) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, function);
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            const std::string message = std::string("Sampo: ") + function + " is unavailable in this Lua state";
            return lua51::fail(state, message.c_str());
        }

        lua51::pushNumber(state, value);
        if (lua51::pcall(state, 1, 1, 0) != 0) {
            const char* const error = lua51::toString(state, -1);
            std::string message = std::string(function) + " failed";
            if (error != nullptr) {
                message.append(": ").append(error);
            }
            lua51::setTop(state, top);
            return lua51::fail(state, message.c_str());
        }
        return 1;
    }

    int keyInput(lua51::lua_State* state, const char* name, const char* function) {
        if (lua51::getTop(state) != 1) {
            const std::string message = std::string("input.") + name + "(key): expected one key";
            return lua51::fail(state, message.c_str());
        }

        double keyCode = 0.0;
        const int valueType = lua51::type(state, 1);
        if (valueType == lua51::typeNumber) {
            keyCode = lua51::toNumber(state, 1);
        } else if (valueType == lua51::typeString) {
            const char* const value = lua51::toString(state, 1);
            std::string label;
            if (value != nullptr) {
                label = upper(value);
            }
            if (label.empty() || !findKeyCode(state, label, keyCode)) {
                const std::string message = "Sampo: unknown Noita key identifier " + label;
                return lua51::fail(state, message.c_str());
            }
        } else {
            const std::string message = std::string("input.") + name + "(key): key must be a number or string";
            return lua51::fail(state, message.c_str());
        }

        return callInput(state, function, keyCode);
    }

    int __cdecl keyDown(lua51::lua_State* state) {
        return keyInput(state, "KeyDown", "InputIsKeyDown");
    }

    int __cdecl keyPressed(lua51::lua_State* state) {
        return keyInput(state, "KeyPressed", "InputIsKeyJustDown");
    }

    int __cdecl keyReleased(lua51::lua_State* state) {
        return keyInput(state, "KeyReleased", "InputIsKeyJustUp");
    }

    int __cdecl leftMouseDown(lua51::lua_State* state) {
        return callInput(state, "InputIsMouseButtonDown", 1.0);
    }

    int __cdecl leftMousePressed(lua51::lua_State* state) {
        return callInput(state, "InputIsMouseButtonJustDown", 1.0);
    }

    int __cdecl leftMouseReleased(lua51::lua_State* state) {
        return callInput(state, "InputIsMouseButtonJustUp", 1.0);
    }

    int __cdecl rightMouseDown(lua51::lua_State* state) {
        return callInput(state, "InputIsMouseButtonDown", 2.0);
    }

    int __cdecl rightMousePressed(lua51::lua_State* state) {
        return callInput(state, "InputIsMouseButtonJustDown", 2.0);
    }

    int __cdecl rightMouseReleased(lua51::lua_State* state) {
        return callInput(state, "InputIsMouseButtonJustUp", 2.0);
    }

    bool hasFunction(lua51::lua_State* state, int table, const char* name, lua51::LuaCFunction function) {
        lua51::getField(state, table, name);
        const bool found = lua51::type(state, -1) == lua51::typeFunction && lua51::toFunction(state, -1) == function;
        lua51::pop(state, 1);
        return found;
    }

    void addFunction(lua51::lua_State* state, const char* name, lua51::LuaCFunction function) {
        lua51::pushFunction(state, function);
        lua51::setField(state, -2, name);
    }
}

bool sampo_lua::load(lua51::lua_State* state) {
    if (state == nullptr || !lua51::ready()) {
        return false;
    }

    const int top = lua51::getTop(state);
    lua51::getGlobal(state, "input");
    if (lua51::type(state, -1) == lua51::typeTable) {
        const bool keyDownLoaded = hasFunction(state, -1, "KeyDown", &keyDown);
        const bool keyPressedLoaded = hasFunction(state, -1, "KeyPressed", &keyPressed);
        const bool keyReleasedLoaded = hasFunction(state, -1, "KeyReleased", &keyReleased);
        const bool leftMouseDownLoaded = hasFunction(state, -1, "LeftMouseDown", &leftMouseDown);
        const bool leftMousePressedLoaded = hasFunction(state, -1, "LeftMousePressed", &leftMousePressed);
        const bool leftMouseReleasedLoaded = hasFunction(state, -1, "LeftMouseReleased", &leftMouseReleased);
        const bool rightMouseDownLoaded = hasFunction(state, -1, "RightMouseDown", &rightMouseDown);
        const bool rightMousePressedLoaded = hasFunction(state, -1, "RightMousePressed", &rightMousePressed);
        const bool rightMouseReleasedLoaded = hasFunction(state, -1, "RightMouseReleased", &rightMouseReleased);
        const bool loaded = keyDownLoaded && keyPressedLoaded && keyReleasedLoaded &&
            leftMouseDownLoaded && leftMousePressedLoaded && leftMouseReleasedLoaded &&
            rightMouseDownLoaded && rightMousePressedLoaded && rightMouseReleasedLoaded;
        if (loaded) {
            lua51::setTop(state, top);
            return true;
        }
    } else {
        lua51::pop(state, 1);
        lua51::createTable(state, 0, 9);
        lua51::pushValue(state, -1);
        lua51::setGlobal(state, "input");
    }

    addFunction(state, "KeyDown", &keyDown);
    addFunction(state, "KeyPressed", &keyPressed);
    addFunction(state, "KeyReleased", &keyReleased);
    addFunction(state, "LeftMouseDown", &leftMouseDown);
    addFunction(state, "LeftMousePressed", &leftMousePressed);
    addFunction(state, "LeftMouseReleased", &leftMouseReleased);
    addFunction(state, "RightMouseDown", &rightMouseDown);
    addFunction(state, "RightMousePressed", &rightMousePressed);
    addFunction(state, "RightMouseReleased", &rightMouseReleased);
    lua51::setTop(state, top);
    return true;
}
