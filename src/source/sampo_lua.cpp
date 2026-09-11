#include "sampo_lua.h"

#include "log.h"
#include "lua51.h"
#include "mod_manager.h"
#include "noita_mainmenu.h"
#include "seed.h"
#include "steam.h"
#include "wand.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

    bool beginCall(lua51::lua_State* state, const char* function, std::string& error) {
        lua51::getGlobal(state, function);
        if (lua51::type(state, -1) == lua51::typeFunction) {
            return true;
        }

        lua51::pop(state, 1);
        error = std::string("Sampo: ") + function + " is unavailable in this Lua state";
        return false;
    }

    bool finishCall(lua51::lua_State* state, const char* function, int arguments, int results, std::string& error) {
        if (lua51::pcall(state, arguments, results, 0) == 0) {
            return true;
        }

        const char* const message = lua51::toString(state, -1);
        error = std::string(function) + " failed";
        if (message != nullptr) {
            error.append(": ").append(message);
        }
        return false;
    }

    bool getTaggedEntities(lua51::lua_State* state, const char* tag, std::vector<int>& entities) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntityGetWithTag", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushString(state, tag);
        if (!finishCall(state, "EntityGetWithTag", 1, 1, error)) {
            lua51::setTop(state, top);
            return false;
        }

        if (lua51::type(state, -1) == lua51::typeTable) {
            int index = 1;
            while (true) {
                lua51::rawGetIndex(state, -1, index);
                if (lua51::type(state, -1) == lua51::typeNil) {
                    lua51::pop(state, 1);
                    break;
                }
                if (lua51::type(state, -1) == lua51::typeNumber) {
                    entities.push_back(static_cast<int>(lua51::toNumber(state, -1)));
                }
                lua51::pop(state, 1);
                ++index;
            }
        }

        lua51::setTop(state, top);
        return true;
    }

    bool getComponent(lua51::lua_State* state, int entity, const char* componentType, int& component, const char* tag = nullptr) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntityGetFirstComponentIncludingDisabled", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, entity);
        lua51::pushString(state, componentType);
        int arguments = 2;
        if (tag != nullptr && tag[0] != '\0') {
            lua51::pushString(state, tag);
            arguments = 3;
        }
        if (!finishCall(state, "EntityGetFirstComponentIncludingDisabled", arguments, 1, error)) {
            lua51::setTop(state, top);
            return false;
        }

        component = 0;
        if (lua51::type(state, -1) == lua51::typeNumber) {
            component = static_cast<int>(lua51::toNumber(state, -1));
        }
        lua51::setTop(state, top);
        return component != 0;
    }

    bool getComponentNumber(lua51::lua_State* state, int entity, const char* componentType, const char* field, double& value) {
        int component = 0;
        if (!getComponent(state, entity, componentType, component)) {
            return false;
        }

        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "ComponentGetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        if (!finishCall(state, "ComponentGetValue2", 2, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }

        value = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool getComponentBoolean(lua51::lua_State* state, int entity, const char* componentType, const char* field, bool& value) {
        int component = 0;
        if (!getComponent(state, entity, componentType, component)) {
            return false;
        }

        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "ComponentGetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        if (!finishCall(state, "ComponentGetValue2", 2, 1, error) || lua51::type(state, -1) != lua51::typeBoolean) {
            lua51::setTop(state, top);
            return false;
        }

        value = lua51::toBoolean(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool getComponentVector(lua51::lua_State* state, int entity, const char* componentType, const char* field, double& x, double& y) {
        int component = 0;
        if (!getComponent(state, entity, componentType, component)) {
            return false;
        }

        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "ComponentGetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        if (!finishCall(state, "ComponentGetValue2", 2, 2, error) || lua51::type(state, -2) != lua51::typeNumber || lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }

        x = lua51::toNumber(state, -2);
        y = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool setComponentNumber(lua51::lua_State* state, int entity, const char* componentType, const char* field, double value) {
        int component = 0;
        if (!getComponent(state, entity, componentType, component)) {
            return false;
        }

        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "ComponentSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        lua51::pushNumber(state, value);
        const bool result = finishCall(state, "ComponentSetValue2", 3, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentBoolean(lua51::lua_State* state, int entity, const char* componentType, const char* field, bool value) {
        int component = 0;
        if (!getComponent(state, entity, componentType, component)) {
            return false;
        }

        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "ComponentSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        lua51::pushBoolean(state, value);
        const bool result = finishCall(state, "ComponentSetValue2", 3, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentVector(lua51::lua_State* state, int entity, const char* componentType, const char* field, double x, double y) {
        int component = 0;
        if (!getComponent(state, entity, componentType, component)) {
            return false;
        }

        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "ComponentSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        const bool result = finishCall(state, "ComponentSetValue2", 4, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentNumberValue(lua51::lua_State* state, int component, const char* field, double value, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        lua51::pushNumber(state, value);
        const bool result = finishCall(state, "ComponentSetValue2", 3, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentStringValue(lua51::lua_State* state, int component, const char* field, const char* value, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        lua51::pushString(state, value);
        const bool result = finishCall(state, "ComponentSetValue2", 3, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentBooleanValue(lua51::lua_State* state, int component, const char* field, bool value, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, field);
        lua51::pushBoolean(state, value);
        const bool result = finishCall(state, "ComponentSetValue2", 3, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentObjectNumber(lua51::lua_State* state, int component, const char* object, const char* field, double value, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentObjectSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, object);
        lua51::pushString(state, field);
        lua51::pushNumber(state, value);
        const bool result = finishCall(state, "ComponentObjectSetValue2", 4, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool setComponentObjectBoolean(lua51::lua_State* state, int component, const char* object, const char* field, bool value, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentObjectSetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, object);
        lua51::pushString(state, field);
        lua51::pushBoolean(state, value);
        const bool result = finishCall(state, "ComponentObjectSetValue2", 4, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool getComponentObjectNumber(lua51::lua_State* state, int component, const char* object, const char* field, double& value, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentObjectGetValue2", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, component);
        lua51::pushString(state, object);
        lua51::pushString(state, field);
        if (!finishCall(state, "ComponentObjectGetValue2", 3, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }

        value = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool readNumberField(lua51::lua_State* state, int table, const char* name, double& value, bool& found) {
        lua51::getField(state, table, name);
        found = lua51::type(state, -1) != lua51::typeNil;
        if (!found) {
            lua51::pop(state, 1);
            return true;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::pop(state, 1);
            return false;
        }

        value = lua51::toNumber(state, -1);
        lua51::pop(state, 1);
        return true;
    }

    bool readStringField(lua51::lua_State* state, int table, const char* name, std::string& value, bool& found) {
        lua51::getField(state, table, name);
        found = lua51::type(state, -1) != lua51::typeNil;
        if (!found) {
            lua51::pop(state, 1);
            return true;
        }
        if (lua51::type(state, -1) != lua51::typeString) {
            lua51::pop(state, 1);
            return false;
        }

        const char* const text = lua51::toString(state, -1);
        if (text == nullptr) {
            value.clear();
        } else {
            value = text;
        }
        lua51::pop(state, 1);
        return true;
    }

    bool readBooleanField(lua51::lua_State* state, int table, const char* name, bool& value, bool& found) {
        lua51::getField(state, table, name);
        found = lua51::type(state, -1) != lua51::typeNil;
        if (!found) {
            lua51::pop(state, 1);
            return true;
        }
        if (lua51::type(state, -1) != lua51::typeBoolean) {
            lua51::pop(state, 1);
            return false;
        }

        value = lua51::toBoolean(state, -1);
        lua51::pop(state, 1);
        return true;
    }

    bool optionalTable(lua51::lua_State* state, int index) {
        const int valueType = lua51::type(state, index);
        return valueType == lua51::typeNone || valueType == lua51::typeNil || valueType == lua51::typeTable;
    }

    bool getPosition(lua51::lua_State* state, int entity, double& x, double& y) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntityGetTransform", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, entity);
        if (!finishCall(state, "EntityGetTransform", 1, 2, error) || lua51::type(state, -2) != lua51::typeNumber || lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }

        x = lua51::toNumber(state, -2);
        y = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool setPosition(lua51::lua_State* state, int entity, double x, double y) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntitySetTransform", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, entity);
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        const bool result = finishCall(state, "EntitySetTransform", 3, 0, error);
        lua51::setTop(state, top);
        return result;
    }

    bool entityHasTag(lua51::lua_State* state, int entity, const char* tag, bool& result) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntityHasTag", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, entity);
        lua51::pushString(state, tag);
        if (!finishCall(state, "EntityHasTag", 2, 1, error)) {
            lua51::setTop(state, top);
            return false;
        }

        result = lua51::toBoolean(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool entityIsAlive(lua51::lua_State* state, int entity, bool& result) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntityGetIsAlive", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, entity);
        if (!finishCall(state, "EntityGetIsAlive", 1, 1, error)) {
            lua51::setTop(state, top);
            return false;
        }

        result = lua51::toBoolean(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool findPolymorphedPlayer(lua51::lua_State* state, int& player) {
        std::vector<int> entities;
        if (!getTaggedEntities(state, "polymorphed", entities)) {
            return false;
        }

        for (const int entity : entities) {
            bool isPlayer = false;
            if (getComponentBoolean(state, entity, "GameStatsComponent", "is_player", isPlayer) && isPlayer) {
                player = entity;
                return true;
            }
        }
        return false;
    }

    bool findPlayer(lua51::lua_State* state, int& player) {
        std::vector<int> players;
        if (getTaggedEntities(state, "player_unit", players) && !players.empty()) {
            player = players.front();
            return player != 0;
        }
        return findPolymorphedPlayer(state, player);
    }

    bool getInventoryItems(lua51::lua_State* state, int player, std::vector<int>& items) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "GameGetAllInventoryItems", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, player);
        if (!finishCall(state, "GameGetAllInventoryItems", 1, 1, error)) {
            lua51::setTop(state, top);
            return false;
        }

        if (lua51::type(state, -1) == lua51::typeTable) {
            int index = 1;
            while (true) {
                lua51::rawGetIndex(state, -1, index);
                if (lua51::type(state, -1) == lua51::typeNil) {
                    lua51::pop(state, 1);
                    break;
                }
                if (lua51::type(state, -1) == lua51::typeNumber) {
                    items.push_back(static_cast<int>(lua51::toNumber(state, -1)));
                }
                lua51::pop(state, 1);
                ++index;
            }
        }

        lua51::setTop(state, top);
        return true;
    }

    bool findChild(lua51::lua_State* state, int parent, const char* wanted, int& child) {
        const int top = lua51::getTop(state);
        std::string error;
        if (!beginCall(state, "EntityGetAllChildren", error)) {
            lua51::setTop(state, top);
            return false;
        }

        lua51::pushNumber(state, parent);
        if (!finishCall(state, "EntityGetAllChildren", 1, 1, error) || lua51::type(state, -1) != lua51::typeTable) {
            lua51::setTop(state, top);
            return false;
        }

        const int children = lua51::getTop(state);
        int index = 1;
        while (true) {
            lua51::rawGetIndex(state, children, index);
            if (lua51::type(state, -1) == lua51::typeNil) {
                lua51::pop(state, 1);
                break;
            }

            const int entity = static_cast<int>(lua51::toNumber(state, -1));
            lua51::pop(state, 1);
            if (!beginCall(state, "EntityGetName", error)) {
                lua51::setTop(state, top);
                return false;
            }
            lua51::pushNumber(state, entity);
            if (!finishCall(state, "EntityGetName", 1, 1, error)) {
                lua51::setTop(state, top);
                return false;
            }

            const char* const name = lua51::toString(state, -1);
            const bool matched = name != nullptr && std::string(name) == wanted;
            lua51::pop(state, 1);
            if (matched) {
                child = entity;
                lua51::setTop(state, top);
                return true;
            }
            ++index;
        }

        lua51::setTop(state, top);
        return false;
    }

    void pushEntityTable(lua51::lua_State* state, const std::vector<int>& entities) {
        lua51::createTable(state, static_cast<int>(entities.size()), 0);
        const int table = lua51::getTop(state);
        int index = 1;
        for (const int entity : entities) {
            lua51::pushNumber(state, entity);
            lua51::rawSetIndex(state, table, index);
            ++index;
        }
    }

    int playerError(lua51::lua_State* state, const char* function, const char* message) {
        const std::string error = std::string("player.") + function + ": " + message;
        return lua51::fail(state, error.c_str());
    }

    int getPlayerNumber(lua51::lua_State* state, const char* function, const char* componentType, const char* field, double multiplier) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, function, "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, function, "no active player entity");
        }

        double value = 0.0;
        if (!getComponentNumber(state, player, componentType, field, value)) {
            return playerError(state, function, "could not read the required component value");
        }

        lua51::pushNumber(state, value * multiplier);
        return 1;
    }

    int setPlayerNumber(lua51::lua_State* state, const char* function, const char* componentType, const char* field, double divisor) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return playerError(state, function, "expected one number");
        }

        const double value = lua51::toNumber(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, function, "no active player entity");
        }
        if (!setComponentNumber(state, player, componentType, field, value / divisor)) {
            return playerError(state, function, "could not set the required component value");
        }
        return 0;
    }

    int getPlayerBoolean(lua51::lua_State* state, const char* function, const char* componentType, const char* field) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, function, "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, function, "no active player entity");
        }

        bool value = false;
        if (!getComponentBoolean(state, player, componentType, field, value)) {
            return playerError(state, function, "could not read the required component value");
        }

        lua51::pushBoolean(state, value);
        return 1;
    }

    int setPlayerBoolean(lua51::lua_State* state, const char* function, const char* componentType, const char* field) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeBoolean) {
            return playerError(state, function, "expected one boolean");
        }

        const bool value = lua51::toBoolean(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, function, "no active player entity");
        }
        if (!setComponentBoolean(state, player, componentType, field, value)) {
            return playerError(state, function, "could not set the required component value");
        }
        return 0;
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

    int __cdecl playerTryGetId(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "TryGetId", "expected no arguments");
        }

        int player = 0;
        if (findPlayer(state, player)) {
            lua51::pushNumber(state, player);
        } else {
            lua51::pushNil(state);
        }
        return 1;
    }

    int __cdecl playerGetId(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetId", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetId", "no active player entity");
        }

        lua51::pushNumber(state, player);
        return 1;
    }

    int __cdecl playerExists(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "Exists", "expected no arguments");
        }

        int player = 0;
        lua51::pushBoolean(state, findPlayer(state, player));
        return 1;
    }

    int __cdecl playerIsAlive(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "IsAlive", "expected no arguments");
        }

        int player = 0;
        bool alive = false;
        if (findPlayer(state, player)) {
            entityIsAlive(state, player, alive);
        }
        lua51::pushBoolean(state, alive);
        return 1;
    }

    int __cdecl playerGetPosition(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetPosition", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetPosition", "no active player entity");
        }

        double x = 0.0;
        double y = 0.0;
        if (!getPosition(state, player, x, y)) {
            return playerError(state, "GetPosition", "could not read the player position");
        }

        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        return 2;
    }

    int __cdecl playerSetPosition(lua51::lua_State* state) {
        if (lua51::getTop(state) != 2 || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeNumber) {
            return playerError(state, "SetPosition", "expected x and y numbers");
        }

        const double x = lua51::toNumber(state, 1);
        const double y = lua51::toNumber(state, 2);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "SetPosition", "no active player entity");
        }
        if (!setPosition(state, player, x, y)) {
            return playerError(state, "SetPosition", "could not set the player position");
        }
        return 0;
    }

    int __cdecl playerGetVelocity(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetVelocity", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetVelocity", "no active player entity");
        }

        double x = 0.0;
        double y = 0.0;
        if (!getComponentVector(state, player, "CharacterDataComponent", "mVelocity", x, y)) {
            return playerError(state, "GetVelocity", "could not read the player velocity");
        }

        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        return 2;
    }

    int __cdecl playerSetVelocity(lua51::lua_State* state) {
        if (lua51::getTop(state) != 2 || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeNumber) {
            return playerError(state, "SetVelocity", "expected x and y numbers");
        }

        const double x = lua51::toNumber(state, 1);
        const double y = lua51::toNumber(state, 2);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "SetVelocity", "no active player entity");
        }
        if (!setComponentVector(state, player, "CharacterDataComponent", "mVelocity", x, y)) {
            return playerError(state, "SetVelocity", "could not set the player velocity");
        }
        return 0;
    }

    int __cdecl playerGetAimDirection(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetAimDirection", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetAimDirection", "no active player entity");
        }

        double x = 0.0;
        double y = 0.0;
        if (!getComponentVector(state, player, "ControlsComponent", "mAimingVectorNormalized", x, y)) {
            return playerError(state, "GetAimDirection", "could not read the player aim direction");
        }

        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        return 2;
    }

    int __cdecl playerGetAir(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetAir", "DamageModelComponent", "air_in_lungs", 1.0);
    }

    int __cdecl playerSetAir(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetAir", "DamageModelComponent", "air_in_lungs", 1.0);
    }

    int __cdecl playerGetMaxAir(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetMaxAir", "DamageModelComponent", "air_in_lungs_max", 1.0);
    }

    int __cdecl playerSetMaxAir(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetMaxAir", "DamageModelComponent", "air_in_lungs_max", 1.0);
    }

    int __cdecl playerGetHealth(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetHealth", "DamageModelComponent", "hp", 25.0);
    }

    int __cdecl playerSetHealth(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetHealth", "DamageModelComponent", "hp", 25.0);
    }

    int __cdecl playerGetMaxHealth(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetMaxHealth", "DamageModelComponent", "max_hp", 25.0);
    }

    int __cdecl playerSetMaxHealth(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetMaxHealth", "DamageModelComponent", "max_hp", 25.0);
    }

    int __cdecl playerGetNeedsAir(lua51::lua_State* state) {
        return getPlayerBoolean(state, "GetNeedsAir", "DamageModelComponent", "air_needed");
    }

    int __cdecl playerSetNeedsAir(lua51::lua_State* state) {
        return setPlayerBoolean(state, "SetNeedsAir", "DamageModelComponent", "air_needed");
    }

    int __cdecl playerGetClimbHeight(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetClimbHeight", "CharacterDataComponent", "climb_over_y", 1.0);
    }

    int __cdecl playerSetClimbHeight(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetClimbHeight", "CharacterDataComponent", "climb_over_y", 1.0);
    }

    int __cdecl playerGetJetpack(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetJetpack", "CharacterDataComponent", "fly_time_max", 1.0);
    }

    int __cdecl playerSetJetpack(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetJetpack", "CharacterDataComponent", "fly_time_max", 1.0);
    }

    int __cdecl playerGetJetpackRecharge(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetJetpackRecharge", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetJetpackRecharge", "no active player entity");
        }

        double air = 0.0;
        double ground = 0.0;
        if (!getComponentNumber(state, player, "CharacterDataComponent", "fly_recharge_spd", air) || !getComponentNumber(state, player, "CharacterDataComponent", "fly_recharge_spd_ground", ground)) {
            return playerError(state, "GetJetpackRecharge", "could not read the jetpack recharge values");
        }

        lua51::pushNumber(state, air);
        lua51::pushNumber(state, ground);
        return 2;
    }

    int __cdecl playerGetGold(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetGold", "WalletComponent", "money", 1.0);
    }

    int __cdecl playerSetGold(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return playerError(state, "SetGold", "expected one number");
        }

        const double value = std::floor(lua51::toNumber(state, 1));
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "SetGold", "no active player entity");
        }
        if (!setComponentNumber(state, player, "WalletComponent", "money", value)) {
            return playerError(state, "SetGold", "could not set the player gold");
        }
        return 0;
    }

    int __cdecl playerGetSpentGold(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetSpentGold", "WalletComponent", "money_spent", 1.0);
    }

    int __cdecl playerGetStomachFullness(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetStomachFullness", "IngestionComponent", "ingestion_size", 1.0);
    }

    int __cdecl playerSetStomachFullness(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetStomachFullness", "IngestionComponent", "ingestion_size", 1.0);
    }

    int __cdecl playerGetStomachSize(lua51::lua_State* state) {
        return getPlayerNumber(state, "GetStomachSize", "IngestionComponent", "ingestion_capacity", 1.0);
    }

    int __cdecl playerSetStomachSize(lua51::lua_State* state) {
        return setPlayerNumber(state, "SetStomachSize", "IngestionComponent", "ingestion_capacity", 1.0);
    }

    int __cdecl playerHeal(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return playerError(state, "Heal", "expected one number");
        }

        const double amount = lua51::toNumber(state, 1);
        if (amount < 0.0) {
            return playerError(state, "Heal", "amount cannot be negative");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "Heal", "no active player entity");
        }

        double health = 0.0;
        double maxHealth = 0.0;
        if (!getComponentNumber(state, player, "DamageModelComponent", "hp", health) || !getComponentNumber(state, player, "DamageModelComponent", "max_hp", maxHealth)) {
            return playerError(state, "Heal", "could not read the player health");
        }

        health += amount / 25.0;
        if (health > maxHealth) {
            health = maxHealth;
        }
        if (!setComponentNumber(state, player, "DamageModelComponent", "hp", health)) {
            return playerError(state, "Heal", "could not set the player health");
        }

        lua51::pushNumber(state, health * 25.0);
        return 1;
    }

    int __cdecl playerDamage(lua51::lua_State* state) {
        if (lua51::getTop(state) != 3 || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || lua51::type(state, 3) != lua51::typeString) {
            return playerError(state, "Damage", "expected amount, damage type and description");
        }

        const double amount = lua51::toNumber(state, 1);
        const char* const damageType = lua51::toString(state, 2);
        const char* const description = lua51::toString(state, 3);
        if (amount < 0.0) {
            return playerError(state, "Damage", "amount cannot be negative");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "Damage", "no active player entity");
        }

        std::string error;
        if (!beginCall(state, "EntityInflictDamage", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, player);
        lua51::pushNumber(state, amount / 25.0);
        lua51::pushString(state, damageType);
        lua51::pushString(state, description);
        lua51::pushString(state, "NONE");
        lua51::pushNumber(state, 0.0);
        lua51::pushNumber(state, 0.0);
        if (!finishCall(state, "EntityInflictDamage", 7, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 0;
    }

    int __cdecl playerGetBiome(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetBiome", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetBiome", "no active player entity");
        }

        double x = 0.0;
        double y = 0.0;
        if (!getPosition(state, player, x, y)) {
            return playerError(state, "GetBiome", "could not read the player position");
        }

        std::string error;
        if (!beginCall(state, "BiomeMapGetName", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        if (!finishCall(state, "BiomeMapGetName", 2, 1, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 1;
    }

    int __cdecl playerGetInventoryEntity(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetInventoryEntity", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetInventoryEntity", "no active player entity");
        }

        int inventory = 0;
        if (findChild(state, player, "inventory_quick", inventory)) {
            lua51::pushNumber(state, inventory);
        } else {
            lua51::pushNil(state);
        }
        return 1;
    }

    int __cdecl playerGetInventoryItems(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetInventoryItems", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetInventoryItems", "no active player entity");
        }

        std::vector<int> items;
        if (!getInventoryItems(state, player, items)) {
            return playerError(state, "GetInventoryItems", "could not read the player inventory");
        }
        pushEntityTable(state, items);
        return 1;
    }

    int __cdecl playerGetWands(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetWands", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetWands", "no active player entity");
        }

        std::vector<int> items;
        if (!getInventoryItems(state, player, items)) {
            return playerError(state, "GetWands", "could not read the player inventory");
        }

        std::vector<int> wands;
        for (const int item : items) {
            bool isWand = false;
            if (entityHasTag(state, item, "wand", isWand) && isWand) {
                wands.push_back(item);
            }
        }
        pushEntityTable(state, wands);
        return 1;
    }

    int getActiveItem(lua51::lua_State* state, const char* function, bool wandOnly) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, function, "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, function, "no active player entity");
        }

        double activeItem = 0.0;
        if (!getComponentNumber(state, player, "Inventory2Component", "mActiveItem", activeItem)) {
            return playerError(state, function, "could not read the active item");
        }

        const int item = static_cast<int>(activeItem);
        if (item == 0) {
            lua51::pushNil(state);
            return 1;
        }
        if (wandOnly) {
            bool isWand = false;
            if (!entityHasTag(state, item, "wand", isWand) || !isWand) {
                lua51::pushNil(state);
                return 1;
            }
        }

        lua51::pushNumber(state, item);
        return 1;
    }

    int __cdecl playerGetActiveItem(lua51::lua_State* state) {
        return getActiveItem(state, "GetActiveItem", false);
    }

    int __cdecl playerGetActiveWand(lua51::lua_State* state) {
        return getActiveItem(state, "GetActiveWand", true);
    }

    bool giveItem(lua51::lua_State* state, int player, int item, std::string& error) {
        if (!beginCall(state, "GamePickUpInventoryItem", error)) {
            return false;
        }
        lua51::pushNumber(state, player);
        lua51::pushNumber(state, item);
        lua51::pushBoolean(state, true);
        return finishCall(state, "GamePickUpInventoryItem", 3, 0, error);
    }

    int __cdecl playerGiveItem(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return playerError(state, "GiveItem", "expected one entity ID");
        }

        const int item = static_cast<int>(lua51::toNumber(state, 1));
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GiveItem", "no active player entity");
        }

        std::string error;
        if (!giveItem(state, player, item, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, item);
        return 1;
    }

    int __cdecl playerGiveSpell(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return playerError(state, "GiveSpell", "expected one spell ID");
        }

        const char* const spell = lua51::toString(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GiveSpell", "no active player entity");
        }

        double x = 0.0;
        double y = 0.0;
        if (!getPosition(state, player, x, y)) {
            return playerError(state, "GiveSpell", "could not read the player position");
        }

        std::string error;
        if (!beginCall(state, "CreateItemActionEntity", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushString(state, spell);
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        if (!finishCall(state, "CreateItemActionEntity", 3, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "GiveSpell", "could not create the spell entity");
        }

        const int item = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (!giveItem(state, player, item, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, item);
        return 1;
    }

    int __cdecl playerHasEffect(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return playerError(state, "HasEffect", "expected one effect name");
        }

        const char* const effect = lua51::toString(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "HasEffect", "no active player entity");
        }

        std::string error;
        if (!beginCall(state, "GameGetGameEffectCount", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, player);
        lua51::pushString(state, effect);
        if (!finishCall(state, "GameGetGameEffectCount", 2, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "HasEffect", "could not read the effect count");
        }

        const bool found = lua51::toNumber(state, -1) > 0.0;
        lua51::pop(state, 1);
        lua51::pushBoolean(state, found);
        return 1;
    }

    int __cdecl playerAddEffect(lua51::lua_State* state) {
        if (lua51::getTop(state) != 2 || lua51::type(state, 1) != lua51::typeString || lua51::type(state, 2) != lua51::typeNumber) {
            return playerError(state, "AddEffect", "expected an effect name and duration in frames");
        }

        const char* const effect = lua51::toString(state, 1);
        const double duration = std::floor(lua51::toNumber(state, 2));
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "AddEffect", "no active player entity");
        }

        std::string error;
        if (!beginCall(state, "GetGameEffectLoadTo", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, player);
        lua51::pushString(state, effect);
        lua51::pushBoolean(state, true);
        if (!finishCall(state, "GetGameEffectLoadTo", 3, 2, error) || lua51::type(state, -2) != lua51::typeNumber || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "AddEffect", "could not create the game effect");
        }

        const int component = static_cast<int>(lua51::toNumber(state, -2));
        const int entity = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 2);
        if (!beginCall(state, "ComponentSetValue2", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, component);
        lua51::pushString(state, "frames");
        lua51::pushNumber(state, duration);
        if (!finishCall(state, "ComponentSetValue2", 3, 0, error)) {
            return lua51::fail(state, error.c_str());
        }

        lua51::pushNumber(state, entity);
        return 1;
    }

    int __cdecl playerRemoveEffect(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return playerError(state, "RemoveEffect", "expected one effect name");
        }

        const char* const effect = lua51::toString(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "RemoveEffect", "no active player entity");
        }

        std::string error;
        if (!beginCall(state, "GameGetGameEffect", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, player);
        lua51::pushString(state, effect);
        if (!finishCall(state, "GameGetGameEffect", 2, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "RemoveEffect", "could not find the game effect");
        }

        const int component = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (component == 0) {
            lua51::pushBoolean(state, false);
            return 1;
        }

        if (!beginCall(state, "ComponentGetEntity", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, component);
        if (!finishCall(state, "ComponentGetEntity", 1, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "RemoveEffect", "could not find the effect entity");
        }

        const int entity = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (entity == 0) {
            lua51::pushBoolean(state, false);
            return 1;
        }

        if (!beginCall(state, "EntityKill", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, entity);
        if (!finishCall(state, "EntityKill", 1, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushBoolean(state, true);
        return 1;
    }

    bool loadPerkFunctions(lua51::lua_State* state, std::string& error) {
        if (!beginCall(state, "dofile_once", error)) {
            return false;
        }
        lua51::pushString(state, "data/scripts/perks/perk.lua");
        return finishCall(state, "dofile_once", 1, 0, error);
    }

    int __cdecl playerHasPerk(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return playerError(state, "HasPerk", "expected one perk ID");
        }

        const char* const perk = lua51::toString(state, 1);
        std::string error;
        if (!loadPerkFunctions(state, error)) {
            return lua51::fail(state, error.c_str());
        }
        if (!beginCall(state, "get_perk_picked_flag_name", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushString(state, perk);
        if (!finishCall(state, "get_perk_picked_flag_name", 1, 1, error) || lua51::type(state, -1) != lua51::typeString) {
            return playerError(state, "HasPerk", "could not resolve the perk flag");
        }

        const char* const flag = lua51::toString(state, -1);
        if (!beginCall(state, "GameHasFlagRun", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushString(state, flag);
        if (!finishCall(state, "GameHasFlagRun", 1, 1, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 1;
    }

    int __cdecl playerAddPerk(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return playerError(state, "AddPerk", "expected one perk ID");
        }

        const char* const perk = lua51::toString(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "AddPerk", "no active player entity");
        }

        double x = 0.0;
        double y = 0.0;
        if (!getPosition(state, player, x, y)) {
            return playerError(state, "AddPerk", "could not read the player position");
        }

        std::string error;
        if (!loadPerkFunctions(state, error)) {
            return lua51::fail(state, error.c_str());
        }
        if (!beginCall(state, "perk_spawn", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        lua51::pushString(state, perk);
        if (!finishCall(state, "perk_spawn", 3, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "AddPerk", "could not spawn the perk");
        }

        const int perkEntity = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (!beginCall(state, "perk_pickup", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, perkEntity);
        lua51::pushNumber(state, player);
        lua51::pushString(state, "");
        lua51::pushBoolean(state, true);
        lua51::pushBoolean(state, false);
        if (!finishCall(state, "perk_pickup", 5, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 0;
    }

    int __cdecl playerGetIsIgnored(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "GetIsIgnored", "expected no arguments");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "GetIsIgnored", "no active player entity");
        }

        double herd = 0.0;
        if (!getComponentNumber(state, player, "GenomeDataComponent", "herd_id", herd)) {
            return playerError(state, "GetIsIgnored", "could not read the player herd");
        }

        std::string error;
        if (!beginCall(state, "HerdIdToString", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, herd);
        if (!finishCall(state, "HerdIdToString", 1, 1, error) || lua51::type(state, -1) != lua51::typeString) {
            return playerError(state, "GetIsIgnored", "could not resolve the player herd");
        }

        const char* const herdName = lua51::toString(state, -1);
        const bool ignored = herdName == nullptr || std::string(herdName) != "player";
        lua51::pop(state, 1);
        lua51::pushBoolean(state, ignored);
        return 1;
    }

    int __cdecl playerSetIsIgnored(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeBoolean) {
            return playerError(state, "SetIsIgnored", "expected one boolean");
        }

        const bool ignored = lua51::toBoolean(state, 1);
        int player = 0;
        if (!findPlayer(state, player)) {
            return playerError(state, "SetIsIgnored", "no active player entity");
        }

        std::string error;
        if (!beginCall(state, "StringToHerdId", error)) {
            return lua51::fail(state, error.c_str());
        }
        if (ignored) {
            lua51::pushString(state, "healer");
        } else {
            lua51::pushString(state, "player");
        }
        if (!finishCall(state, "StringToHerdId", 1, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return playerError(state, "SetIsIgnored", "could not resolve the new herd");
        }

        const double herd = lua51::toNumber(state, -1);
        lua51::pop(state, 1);
        if (!setComponentNumber(state, player, "GenomeDataComponent", "herd_id", herd)) {
            return playerError(state, "SetIsIgnored", "could not set the player herd");
        }
        return 0;
    }

    int __cdecl playerIsPolymorphed(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return playerError(state, "IsPolymorphed", "expected no arguments");
        }

        int player = 0;
        const bool polymorphed = findPolymorphedPlayer(state, player);
        lua51::pushBoolean(state, polymorphed);
        if (polymorphed) {
            lua51::pushNumber(state, player);
        } else {
            lua51::pushNil(state);
        }
        return 2;
    }

    int globalError(lua51::lua_State* state, const char* function, const char* message) {
        const std::string error = std::string(function) + ": " + message;
        return lua51::fail(state, error.c_str());
    }

    const char* optionalTag(lua51::lua_State* state, int index) {
        if (lua51::type(state, index) == lua51::typeString) {
            return lua51::toString(state, index);
        }
        return nullptr;
    }

    int __cdecl entityGetComponentValue(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 3 && arguments != 4) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || lua51::type(state, 3) != lua51::typeString || (arguments == 4 && lua51::type(state, 4) != lua51::typeString)) {
            return globalError(state, "EntityGetComponentValue", "expected entity, component type, field, and an optional tag");
        }

        int component = 0;
        if (!getComponent(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), component, optionalTag(state, 4))) {
            lua51::pushNil(state);
            return 1;
        }

        std::string error;
        if (!beginCall(state, "ComponentGetValue2", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, component);
        lua51::pushValue(state, 3);
        if (!finishCall(state, "ComponentGetValue2", 2, lua51::multiReturn, error)) {
            return lua51::fail(state, error.c_str());
        }
        return lua51::getTop(state) - arguments;
    }

    int __cdecl entitySetComponentValue(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 4 && arguments != 5) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || lua51::type(state, 3) != lua51::typeString || (arguments == 5 && lua51::type(state, 5) != lua51::typeString)) {
            return globalError(state, "EntitySetComponentValue", "expected entity, component type, field, value, and an optional tag");
        }

        int component = 0;
        if (!getComponent(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), component, optionalTag(state, 5))) {
            return globalError(state, "EntitySetComponentValue", "required component was not found");
        }

        std::string error;
        if (!beginCall(state, "ComponentSetValue2", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, component);
        lua51::pushValue(state, 3);
        lua51::pushValue(state, 4);
        if (!finishCall(state, "ComponentSetValue2", 3, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 0;
    }

    int __cdecl entityGetComponentObjectValue(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 4 && arguments != 5) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || lua51::type(state, 3) != lua51::typeString || lua51::type(state, 4) != lua51::typeString || (arguments == 5 && lua51::type(state, 5) != lua51::typeString)) {
            return globalError(state, "EntityGetComponentObjectValue", "expected entity, component type, object, field, and an optional tag");
        }

        int component = 0;
        if (!getComponent(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), component, optionalTag(state, 5))) {
            lua51::pushNil(state);
            return 1;
        }

        std::string error;
        if (!beginCall(state, "ComponentObjectGetValue2", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, component);
        lua51::pushValue(state, 3);
        lua51::pushValue(state, 4);
        if (!finishCall(state, "ComponentObjectGetValue2", 3, lua51::multiReturn, error)) {
            return lua51::fail(state, error.c_str());
        }
        return lua51::getTop(state) - arguments;
    }

    int __cdecl entitySetComponentObjectValue(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 5 && arguments != 6) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || lua51::type(state, 3) != lua51::typeString || lua51::type(state, 4) != lua51::typeString || (arguments == 6 && lua51::type(state, 6) != lua51::typeString)) {
            return globalError(state, "EntitySetComponentObjectValue", "expected entity, component type, object, field, value, and an optional tag");
        }

        int component = 0;
        if (!getComponent(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), component, optionalTag(state, 6))) {
            return globalError(state, "EntitySetComponentObjectValue", "required component was not found");
        }

        std::string error;
        if (!beginCall(state, "ComponentObjectSetValue2", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, component);
        lua51::pushValue(state, 3);
        lua51::pushValue(state, 4);
        lua51::pushValue(state, 5);
        if (!finishCall(state, "ComponentObjectSetValue2", 4, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 0;
    }

    int __cdecl entityHasComponent(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 2 && arguments != 3) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || (arguments == 3 && lua51::type(state, 3) != lua51::typeString)) {
            return globalError(state, "EntityHasComponent", "expected entity, component type, and an optional tag");
        }

        int component = 0;
        const bool found = getComponent(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), component, optionalTag(state, 3));
        lua51::pushBoolean(state, found);
        return 1;
    }

    int __cdecl entityRequireComponent(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 2 && arguments != 3) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || (arguments == 3 && lua51::type(state, 3) != lua51::typeString)) {
            return globalError(state, "EntityRequireComponent", "expected entity, component type, and an optional tag");
        }

        int component = 0;
        if (!getComponent(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), component, optionalTag(state, 3))) {
            return globalError(state, "EntityRequireComponent", "required component was not found");
        }
        lua51::pushNumber(state, component);
        return 1;
    }

    int __cdecl entityGetChild(lua51::lua_State* state) {
        if (lua51::getTop(state) != 2 || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString) {
            return globalError(state, "EntityGetChild", "expected an entity and child name");
        }

        int child = 0;
        if (findChild(state, static_cast<int>(lua51::toNumber(state, 1)), lua51::toString(state, 2), child)) {
            lua51::pushNumber(state, child);
        } else {
            lua51::pushNil(state);
        }
        return 1;
    }

    int __cdecl genomeGetHerdId(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return globalError(state, "GenomeGetHerdId", "expected one entity ID");
        }

        double herd = 0.0;
        if (!getComponentNumber(state, static_cast<int>(lua51::toNumber(state, 1)), "GenomeDataComponent", "herd_id", herd)) {
            return globalError(state, "GenomeGetHerdId", "GenomeDataComponent was not found");
        }

        std::string error;
        if (!beginCall(state, "HerdIdToString", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, herd);
        if (!finishCall(state, "HerdIdToString", 1, 1, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 1;
    }

    bool getWorldState(lua51::lua_State* state, int& world, std::string& error) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "GameGetWorldStateEntity", error)) {
            lua51::setTop(state, top);
            return false;
        }
        if (!finishCall(state, "GameGetWorldStateEntity", 0, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }

        world = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);
        return world != 0;
    }

    int __cdecl setWorldTime(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return globalError(state, "SetWorldTime", "expected one number");
        }

        int world = 0;
        std::string error;
        if (!getWorldState(state, world, error)) {
            return globalError(state, "SetWorldTime", "world state is unavailable");
        }

        const double value = (std::max)(0.0, (std::min)(1.0, lua51::toNumber(state, 1)));
        if (!setComponentNumber(state, world, "WorldStateComponent", "time", value)) {
            return globalError(state, "SetWorldTime", "could not update the world time");
        }
        return 0;
    }

    int __cdecl getWorldTime(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return globalError(state, "GetWorldTime", "expected no arguments");
        }

        int world = 0;
        std::string error;
        if (!getWorldState(state, world, error)) {
            return globalError(state, "GetWorldTime", "world state is unavailable");
        }

        double value = 0.0;
        if (!getComponentNumber(state, world, "WorldStateComponent", "time", value)) {
            return globalError(state, "GetWorldTime", "could not read the world time");
        }
        lua51::pushNumber(state, value);
        return 1;
    }

    int __cdecl getWorldSeed(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return globalError(state, "GetWorldSeed", "expected no arguments");
        }

        std::string error;
        if (!beginCall(state, "StatsGetValue", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushString(state, "world_seed");
        if (!finishCall(state, "StatsGetValue", 1, 1, error) || lua51::type(state, -1) != lua51::typeString) {
            return globalError(state, "GetWorldSeed", "world seed is unavailable");
        }

        const char* const seedText = lua51::toString(state, -1);
        char* end = nullptr;
        const double seed = std::strtod(seedText == nullptr ? "" : seedText, &end);
        if (seedText == nullptr || end == seedText) {
            return globalError(state, "GetWorldSeed", "world seed is invalid");
        }
        lua51::pushNumber(state, seed);
        return 1;
    }

    int __cdecl setWorldSeed(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeNumber) {
            return globalError(state, "SetWorldSeed", "expected one number");
        }

        const double value = lua51::toNumber(state, 1);
        if (!std::isfinite(value) || value < 1.0 || value > 4294967295.0 || std::floor(value) != value) {
            return globalError(state, "SetWorldSeed", "seed must be an integer from 1 to 4294967295");
        }
        noita_mainmenu::setWorldSeed(static_cast<std::uint32_t>(value));
        return 0;
    }

    int __cdecl getGoodSeed(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return globalError(state, "GetGoodSeed", "expected no arguments");
        }
        lua51::pushNumber(state, static_cast<double>(seed::getGoodSeed()));
        return 1;
    }

    int __cdecl getBadSeed(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return globalError(state, "GetBadSeed", "expected no arguments");
        }
        lua51::pushNumber(state, static_cast<double>(seed::getBadSeed()));
        return 1;
    }

    int __cdecl spawnFlask(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 3 && arguments != 4) || lua51::type(state, 1) != lua51::typeString || lua51::type(state, 2) != lua51::typeNumber || lua51::type(state, 3) != lua51::typeNumber || (arguments == 4 && lua51::type(state, 4) != lua51::typeNumber)) {
            return globalError(state, "SpawnFlask", "expected material, x, y, and an optional amount");
        }

        const char* const material = lua51::toString(state, 1);
        const double x = lua51::toNumber(state, 2);
        const double y = lua51::toNumber(state, 3);
        double amount = 1000.0;
        if (arguments == 4) {
            amount = lua51::toNumber(state, 4);
        }

        std::string error;
        if (!beginCall(state, "EntityLoad", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushString(state, "data/entities/items/pickup/potion_empty.xml");
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        if (!finishCall(state, "EntityLoad", 3, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return globalError(state, "SpawnFlask", "could not create the flask");
        }

        const int flask = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (!beginCall(state, "AddMaterialInventoryMaterial", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, flask);
        lua51::pushString(state, material);
        lua51::pushNumber(state, amount);
        if (!finishCall(state, "AddMaterialInventoryMaterial", 3, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, flask);
        return 1;
    }

    int __cdecl spawnPerk(lua51::lua_State* state) {
        if (lua51::getTop(state) != 3 || lua51::type(state, 1) != lua51::typeString || lua51::type(state, 2) != lua51::typeNumber || lua51::type(state, 3) != lua51::typeNumber) {
            return globalError(state, "SpawnPerk", "expected perk ID, x, and y");
        }

        std::string error;
        if (!loadPerkFunctions(state, error)) {
            return lua51::fail(state, error.c_str());
        }
        if (!beginCall(state, "perk_spawn", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushValue(state, 2);
        lua51::pushValue(state, 3);
        lua51::pushValue(state, 1);
        if (!finishCall(state, "perk_spawn", 3, 1, error)) {
            return lua51::fail(state, error.c_str());
        }
        return 1;
    }

    bool editWand(lua51::lua_State* state, int wand, int abilityTable, int gunTable, int spriteTable, std::string& error) {
        int ability = 0;
        if (!getComponent(state, wand, "AbilityComponent", ability)) {
            error = "EditWand: AbilityComponent was not found";
            return false;
        }

        int sprite = 0;
        getComponent(state, wand, "SpriteComponent", sprite);
        if (lua51::type(state, abilityTable) == lua51::typeTable) {
            const char* const fields[] = { "mana_max", "mana_charge_speed", "reload_time_frames", "item_recoil_max" };
            for (const char* const field : fields) {
                double value = 0.0;
                bool found = false;
                if (!readNumberField(state, abilityTable, field, value, found)) {
                    error = std::string("EditWand: ability.") + field + " must be a number";
                    return false;
                }
                if (found && !setComponentNumberValue(state, ability, field, value, error)) {
                    return false;
                }
            }

            double castDelay = 0.0;
            bool hasCastDelay = false;
            if (!readNumberField(state, abilityTable, "cast_delay", castDelay, hasCastDelay)) {
                error = "EditWand: ability.cast_delay must be a number";
                return false;
            }
            if (hasCastDelay && !setComponentObjectNumber(state, ability, "gunaction_config", "fire_rate_wait", castDelay, error)) {
                return false;
            }

            const char* const gunActionFields[] = { "spread_degrees", "speed_multiplier" };
            for (const char* const field : gunActionFields) {
                double value = 0.0;
                bool found = false;
                if (!readNumberField(state, abilityTable, field, value, found)) {
                    error = std::string("EditWand: ability.") + field + " must be a number";
                    return false;
                }
                if (found && !setComponentObjectNumber(state, ability, "gunaction_config", field, value, error)) {
                    return false;
                }
            }
        }

        if (lua51::type(state, gunTable) == lua51::typeTable) {
            const char* const fields[] = { "actions_per_round", "reload_time", "deck_capacity" };
            for (const char* const field : fields) {
                double value = 0.0;
                bool found = false;
                if (!readNumberField(state, gunTable, field, value, found)) {
                    error = std::string("EditWand: gun.") + field + " must be a number";
                    return false;
                }
                if (found && !setComponentObjectNumber(state, ability, "gun_config", field, value, error)) {
                    return false;
                }
            }

            bool shuffle = false;
            bool hasShuffle = false;
            if (!readBooleanField(state, gunTable, "shuffle_deck_when_empty", shuffle, hasShuffle)) {
                error = "EditWand: gun.shuffle_deck_when_empty must be a boolean";
                return false;
            }
            if (hasShuffle && !setComponentObjectBoolean(state, ability, "gun_config", "shuffle_deck_when_empty", shuffle, error)) {
                return false;
            }
        }

        if (lua51::type(state, spriteTable) == lua51::typeTable) {
            std::string value;
            bool found = false;
            if (!readStringField(state, spriteTable, "ui_name", value, found)) {
                error = "EditWand: sprite.ui_name must be a string";
                return false;
            }
            if (found && !setComponentStringValue(state, ability, "ui_name", value.c_str(), error)) {
                return false;
            }

            if (!readStringField(state, spriteTable, "sprite", value, found)) {
                error = "EditWand: sprite.sprite must be a string";
                return false;
            }
            if (found) {
                if (!setComponentStringValue(state, ability, "sprite_file", value.c_str(), error)) {
                    return false;
                }
                if (sprite != 0 && !setComponentStringValue(state, sprite, "image_file", value.c_str(), error)) {
                    return false;
                }
            }

            double offset = 0.0;
            if (!readNumberField(state, spriteTable, "offset_x", offset, found)) {
                error = "EditWand: sprite.offset_x must be a number";
                return false;
            }
            if (sprite != 0 && found && !setComponentNumberValue(state, sprite, "offset_x", offset, error)) {
                return false;
            }

            if (!readNumberField(state, spriteTable, "offset_y", offset, found)) {
                error = "EditWand: sprite.offset_y must be a number";
                return false;
            }
            if (sprite != 0 && found && !setComponentNumberValue(state, sprite, "offset_y", offset, error)) {
                return false;
            }
        }

        if (sprite != 0) {
            if (!beginCall(state, "EntityRefreshSprite", error)) {
                return false;
            }
            lua51::pushNumber(state, wand);
            lua51::pushNumber(state, sprite);
            if (!finishCall(state, "EntityRefreshSprite", 2, 0, error)) {
                return false;
            }
        }
        return true;
    }

    int __cdecl editWandGlobal(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if (arguments < 1 || arguments > 4 || lua51::type(state, 1) != lua51::typeNumber || !optionalTable(state, 2) || !optionalTable(state, 3) || !optionalTable(state, 4)) {
            return globalError(state, "EditWand", "expected wand ID and optional ability, gun, and sprite tables");
        }

        const int wand = static_cast<int>(lua51::toNumber(state, 1));
        std::string error;
        if (!editWand(state, wand, 2, 3, 4, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, wand);
        return 1;
    }

    int __cdecl createWand(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if (arguments > 3 || !optionalTable(state, 1) || !optionalTable(state, 2) || !optionalTable(state, 3)) {
            return globalError(state, "CreateWand", "expected optional ability, gun, and sprite tables");
        }

        int player = 0;
        if (!findPlayer(state, player)) {
            return globalError(state, "CreateWand", "no active player entity");
        }
        double x = 0.0;
        double y = 0.0;
        if (!getPosition(state, player, x, y)) {
            return globalError(state, "CreateWand", "could not read the player position");
        }

        std::string error;
        if (!beginCall(state, "EntityLoad", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushString(state, "data/entities/items/starting_wand.xml");
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        if (!finishCall(state, "EntityLoad", 3, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return globalError(state, "CreateWand", "could not create the wand");
        }

        const int wand = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (!editWand(state, wand, 1, 2, 3, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, wand);
        return 1;
    }

    int __cdecl addSpellToWand(lua51::lua_State* state) {
        const int arguments = lua51::getTop(state);
        if ((arguments != 2 && arguments != 3) || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString || (arguments == 3 && lua51::type(state, 3) != lua51::typeBoolean)) {
            return globalError(state, "AddSpellToWand", "expected wand ID, spell ID, and an optional permanent boolean");
        }

        const int wand = static_cast<int>(lua51::toNumber(state, 1));
        const bool permanent = arguments == 3 && lua51::toBoolean(state, 3);
        std::string error;
        if (!beginCall(state, "CreateItemActionEntity", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushValue(state, 2);
        lua51::pushNumber(state, 0.0);
        lua51::pushNumber(state, 0.0);
        if (!finishCall(state, "CreateItemActionEntity", 3, 1, error) || lua51::type(state, -1) != lua51::typeNumber) {
            return globalError(state, "AddSpellToWand", "could not create the spell entity");
        }

        const int action = static_cast<int>(lua51::toNumber(state, -1));
        lua51::pop(state, 1);
        if (permanent) {
            int ability = 0;
            if (!getComponent(state, wand, "AbilityComponent", ability)) {
                return globalError(state, "AddSpellToWand", "wand AbilityComponent was not found");
            }

            double capacity = 0.0;
            if (!getComponentObjectNumber(state, ability, "gun_config", "deck_capacity", capacity, error)) {
                return globalError(state, "AddSpellToWand", "could not read the wand capacity");
            }
            if (!setComponentObjectNumber(state, ability, "gun_config", "deck_capacity", capacity + 1.0, error)) {
                return lua51::fail(state, error.c_str());
            }

            int item = 0;
            if (!getComponent(state, action, "ItemComponent", item)) {
                return globalError(state, "AddSpellToWand", "spell ItemComponent was not found");
            }
            if (!setComponentBooleanValue(state, item, "permanently_attached", true, error)) {
                return lua51::fail(state, error.c_str());
            }
        }

        if (!beginCall(state, "EntityAddChild", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, wand);
        lua51::pushNumber(state, action);
        if (!finishCall(state, "EntityAddChild", 2, 0, error)) {
            return lua51::fail(state, error.c_str());
        }

        if (!beginCall(state, "EntitySetComponentsWithTagEnabled", error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, action);
        lua51::pushString(state, "enabled_in_world");
        lua51::pushBoolean(state, false);
        if (!finishCall(state, "EntitySetComponentsWithTagEnabled", 3, 0, error)) {
            return lua51::fail(state, error.c_str());
        }
        lua51::pushNumber(state, action);
        return 1;
    }

    int taskError(lua51::lua_State* state, const char* function, const char* message) {
        const std::string error = std::string("task.") + function + ": " + message;
        return lua51::fail(state, error.c_str());
    }

    bool utf8ToWide(const char* text, std::size_t length, std::wstring& result) {
        if (length > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }
        if (length == 0) {
            result.clear();
            return true;
        }

        const int textLength = static_cast<int>(length);
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, textLength, nullptr, 0);
        if (required == 0) {
            return false;
        }
        result.resize(required);
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, textLength, result.data(), required) == required;
    }

    bool wideToUtf8(const wchar_t* text, std::size_t length, std::string& result) {
        if (length > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }
        if (length == 0) {
            result.clear();
            return true;
        }

        const int textLength = static_cast<int>(length);
        const int required = WideCharToMultiByte(CP_UTF8, 0, text, textLength, nullptr, 0, nullptr, nullptr);
        if (required == 0) {
            return false;
        }
        result.resize(required);
        return WideCharToMultiByte(CP_UTF8, 0, text, textLength, result.data(), required, nullptr, nullptr) == required;
    }

    bool copyToClipboard(const char* text, std::size_t length) {
        std::wstring wideText;
        if (!utf8ToWide(text, length, wideText)) {
            return false;
        }
        if (!OpenClipboard(nullptr)) {
            return false;
        }
        if (!EmptyClipboard()) {
            CloseClipboard();
            return false;
        }

        const std::size_t bytes = (wideText.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr) {
            CloseClipboard();
            return false;
        }

        void* const destination = GlobalLock(memory);
        if (destination == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        std::memcpy(destination, wideText.c_str(), bytes);
        GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    }

    bool getClipboardText(std::string& result) {
        if (!OpenClipboard(nullptr)) {
            return false;
        }

        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if (data == nullptr) {
            CloseClipboard();
            return false;
        }
        const wchar_t* const text = static_cast<const wchar_t*>(GlobalLock(data));
        if (text == nullptr) {
            CloseClipboard();
            return false;
        }
        const std::size_t length = std::wcslen(text);
        const bool converted = wideToUtf8(text, length, result);
        GlobalUnlock(data);
        CloseClipboard();
        return converted;
    }

    bool getCurrentModSource(lua51::lua_State* state, std::string& id) {
        for (int level = 1; level < 32; ++level) {
            lua51::lua_Debug debug{};
            if (!lua51::getStack(state, level, &debug)) {
                break;
            }
            if (!lua51::getInfo(state, "S", &debug)) {
                continue;
            }

            std::string source;
            if (debug.source != nullptr) {
                source = debug.source;
            } else {
                source = debug.short_src;
            }
            std::replace(source.begin(), source.end(), '\\', '/');
            const std::string normalized = lower(source);
            const std::size_t mods = normalized.find("mods/");
            if (mods == std::string::npos) {
                continue;
            }

            const std::size_t start = mods + 5;
            std::size_t end = source.find('/', start);
            if (end == std::string::npos) {
                end = source.size();
            }
            if (end > start) {
                id = source.substr(start, end - start);
                return true;
            }
        }
        return false;
    }

    int __cdecl taskExecuteTL(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return taskError(state, "ExecuteTL", "expected one script string");
        }

        std::size_t length = 0;
        const char* const script = lua51::toString(state, 1, &length);
        const int originalTop = lua51::getTop(state);
        const int loaded = lua51::loadBuffer(state, script, length, "@Sampo/ExecuteTL", nullptr);
        if (loaded != 0) {
            std::string message = "could not compile the script";
            if (loaded == -1) {
                message = "luaL_loadbufferx is unavailable";
            } else {
                const char* const error = lua51::toString(state, -1);
                if (error != nullptr) {
                    message = error;
                }
            }
            lua51::setTop(state, originalTop);
            return taskError(state, "ExecuteTL", message.c_str());
        }
        if (lua51::pcall(state, 0, lua51::multiReturn, 0) != 0) {
            std::string message = "script execution failed";
            const char* const error = lua51::toString(state, -1);
            if (error != nullptr) {
                message = error;
            }
            lua51::setTop(state, originalTop);
            return taskError(state, "ExecuteTL", message.c_str());
        }
        return lua51::getTop(state) - originalTop;
    }

    int __cdecl taskGetCFunctionPointer(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeFunction) {
            return taskError(state, "GetCFunctionPointer", "expected one function");
        }
        const lua51::LuaCFunction function = lua51::toFunction(state, 1);
        lua51::pushNumber(state, static_cast<double>(reinterpret_cast<std::uintptr_t>(function)));
        return 1;
    }

    int __cdecl taskGetState(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return taskError(state, "GetState", "expected no arguments");
        }
        lua51::pushNumber(state, static_cast<double>(reinterpret_cast<std::uintptr_t>(state)));
        return 1;
    }

    int __cdecl taskGetSteamAchievements(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return taskError(state, "GetSteamAchievements", "expected no arguments");
        }

        const std::vector<std::string> achievements = steam::getAchievements();
        lua51::createTable(state, static_cast<int>(achievements.size()), 0);
        const int table = lua51::getTop(state);
        int index = 1;
        for (const std::string& id : achievements) {
            lua51::pushString(state, id.c_str());
            lua51::rawSetIndex(state, table, index);
            ++index;
        }
        return 1;
    }

    int __cdecl taskSteamHasAchievement(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return taskError(state, "SteamHasAchievement", "expected one achievement ID");
        }
        const char* const id = lua51::toString(state, 1);
        lua51::pushBoolean(state, steam::hasAchievement(id));
        return 1;
    }

    int __cdecl taskSteamRewardAchievement(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return taskError(state, "SteamRewardAchievement", "expected one achievement ID");
        }
        const char* const id = lua51::toString(state, 1);
        const bool rewarded = steam::rewardAchievement(id);
        const char* rewardedText = "false";
        if (rewarded) {
            rewardedText = "true";
        }
        sampo::log::write("SteamRewardAchievement: /arrow Achievement: %s /arrow Rewarded: %s", id, rewardedText);
        lua51::pushBoolean(state, rewarded);
        return 1;
    }

    int __cdecl taskSteamRemoveAchievement(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return taskError(state, "SteamRemoveAchievement", "expected one achievement ID");
        }
        const char* const id = lua51::toString(state, 1);
        const bool removed = steam::removeAchievement(id);
        const char* removedText = "false";
        if (removed) {
            removedText = "true";
        }
        sampo::log::write("SteamRemoveAchievement: /arrow Achievement: %s /arrow Removed: %s", id, removedText);
        lua51::pushBoolean(state, removed);
        return 1;
    }

    int __cdecl taskCopyToClipboard(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return taskError(state, "CopyToClipboard", "expected one string");
        }
        std::size_t length = 0;
        const char* const text = lua51::toString(state, 1, &length);
        lua51::pushBoolean(state, copyToClipboard(text, length));
        return 1;
    }

    int __cdecl taskGetClipboardText(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return taskError(state, "GetClipboardText", "expected no arguments");
        }

        std::string text;
        if (!getClipboardText(text)) {
            lua51::pushNil(state);
            return 1;
        }
        lua51::pushString(state, text.c_str());
        return 1;
    }

    int __cdecl taskGetSampoVersion(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return taskError(state, "GetSampoVersion", "expected no arguments");
        }
        lua51::pushString(state, noita_mainmenu::getSampoBuildText().c_str());
        return 1;
    }

    int __cdecl taskGetCurrentMod(lua51::lua_State* state) {
        if (lua51::getTop(state) != 0) {
            return taskError(state, "GetCurrentMod", "expected no arguments");
        }

        std::string sourceId;
        std::string id;
        std::string path;
        if (!getCurrentModSource(state, sourceId) || !mod_manager::findMod(sourceId, id, path)) {
            lua51::pushNil(state);
            lua51::pushNil(state);
            return 2;
        }
        lua51::pushString(state, id.c_str());
        lua51::pushString(state, path.c_str());
        return 2;
    }

    int __cdecl taskLoadWandFromFile(lua51::lua_State* state) {
        if (lua51::getTop(state) != 1 || lua51::type(state, 1) != lua51::typeString) {
            return taskError(state, "LoadWandFromFile", "expected one .wnd filename or path");
        }

        const char* const filename = lua51::toString(state, 1);
        int entity = 0;
        std::string error;
        if (!wand::load(state, filename, entity, error)) {
            return taskError(state, "LoadWandFromFile", error.c_str());
        }
        lua51::pushNumber(state, static_cast<double>(entity));
        return 1;
    }

    int __cdecl taskExportWand(lua51::lua_State* state) {
        if (lua51::getTop(state) != 2 || lua51::type(state, 1) != lua51::typeNumber || lua51::type(state, 2) != lua51::typeString) {
            return taskError(state, "ExportWand", "expected a wand ID and .wnd filename");
        }

        const int entity = static_cast<int>(lua51::toNumber(state, 1));
        const char* const filename = lua51::toString(state, 2);
        std::string path;
        std::string error;
        if (!wand::save(state, entity, filename, path, error)) {
            return taskError(state, "ExportWand", error.c_str());
        }
        lua51::pushString(state, path.c_str());
        return 1;
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

    struct ApiFunction {
        const char* name;
        lua51::LuaCFunction function;
    };

    bool loadTable(lua51::lua_State* state, const char* name, const ApiFunction* functions, std::size_t count) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, name);
        if (lua51::type(state, -1) != lua51::typeTable) {
            lua51::pop(state, 1);
            lua51::createTable(state, 0, static_cast<int>(count));
            lua51::pushValue(state, -1);
            lua51::setGlobal(state, name);
        }

        for (std::size_t index = 0; index < count; ++index) {
            const ApiFunction& function = functions[index];
            if (!hasFunction(state, -1, function.name, function.function)) {
                addFunction(state, function.name, function.function);
            }
        }

        lua51::setTop(state, top);
        return true;
    }

    bool loadGlobals(lua51::lua_State* state, const ApiFunction* functions, std::size_t count) {
        const int top = lua51::getTop(state);
        for (std::size_t index = 0; index < count; ++index) {
            const ApiFunction& function = functions[index];
            lua51::getGlobal(state, function.name);
            const bool found = lua51::type(state, -1) == lua51::typeFunction && lua51::toFunction(state, -1) == function.function;
            lua51::pop(state, 1);
            if (!found) {
                lua51::pushFunction(state, function.function);
                lua51::setGlobal(state, function.name);
            }
        }

        lua51::setTop(state, top);
        return true;
    }
}

bool sampo_lua::load(lua51::lua_State* state) {
    if (state == nullptr || !lua51::ready()) {
        return false;
    }

    const ApiFunction inputFunctions[] = {
        { "KeyDown", &keyDown },
        { "KeyPressed", &keyPressed },
        { "KeyReleased", &keyReleased },
        { "LeftMouseDown", &leftMouseDown },
        { "LeftMousePressed", &leftMousePressed },
        { "LeftMouseReleased", &leftMouseReleased },
        { "RightMouseDown", &rightMouseDown },
        { "RightMousePressed", &rightMousePressed },
        { "RightMouseReleased", &rightMouseReleased }
    };
    const ApiFunction playerFunctions[] = {
        { "TryGetId", &playerTryGetId },
        { "GetId", &playerGetId },
        { "Exists", &playerExists },
        { "IsAlive", &playerIsAlive },
        { "GetPosition", &playerGetPosition },
        { "SetPosition", &playerSetPosition },
        { "GetVelocity", &playerGetVelocity },
        { "SetVelocity", &playerSetVelocity },
        { "GetAimDirection", &playerGetAimDirection },
        { "GetAir", &playerGetAir },
        { "SetAir", &playerSetAir },
        { "GetMaxAir", &playerGetMaxAir },
        { "SetMaxAir", &playerSetMaxAir },
        { "GetHealth", &playerGetHealth },
        { "SetHealth", &playerSetHealth },
        { "GetMaxHealth", &playerGetMaxHealth },
        { "SetMaxHealth", &playerSetMaxHealth },
        { "Heal", &playerHeal },
        { "Damage", &playerDamage },
        { "GetNeedsAir", &playerGetNeedsAir },
        { "SetNeedsAir", &playerSetNeedsAir },
        { "GetClimbHeight", &playerGetClimbHeight },
        { "SetClimbHeight", &playerSetClimbHeight },
        { "GetJetpack", &playerGetJetpack },
        { "SetJetpack", &playerSetJetpack },
        { "GetJetpackRecharge", &playerGetJetpackRecharge },
        { "GetGold", &playerGetGold },
        { "SetGold", &playerSetGold },
        { "GetSpentGold", &playerGetSpentGold },
        { "GetStomachFullness", &playerGetStomachFullness },
        { "SetStomachFullness", &playerSetStomachFullness },
        { "GetStomachSize", &playerGetStomachSize },
        { "SetStomachSize", &playerSetStomachSize },
        { "GetBiome", &playerGetBiome },
        { "GetInventoryEntity", &playerGetInventoryEntity },
        { "GetInventoryItems", &playerGetInventoryItems },
        { "GetWands", &playerGetWands },
        { "GetActiveItem", &playerGetActiveItem },
        { "GetActiveWand", &playerGetActiveWand },
        { "GiveItem", &playerGiveItem },
        { "GiveSpell", &playerGiveSpell },
        { "GetIsIgnored", &playerGetIsIgnored },
        { "SetIsIgnored", &playerSetIsIgnored },
        { "IsPolymorphed", &playerIsPolymorphed },
        { "HasEffect", &playerHasEffect },
        { "AddEffect", &playerAddEffect },
        { "RemoveEffect", &playerRemoveEffect },
        { "HasPerk", &playerHasPerk },
        { "AddPerk", &playerAddPerk }
    };
    const ApiFunction taskFunctions[] = {
        { "ExecuteTL", &taskExecuteTL },
        { "GetCFunctionPointer", &taskGetCFunctionPointer },
        { "GetState", &taskGetState },
        { "GetSteamAchievements", &taskGetSteamAchievements },
        { "SteamHasAchievement", &taskSteamHasAchievement },
        { "SteamRewardAchievement", &taskSteamRewardAchievement },
        { "SteamRemoveAchievement", &taskSteamRemoveAchievement },
        { "CopyToClipboard", &taskCopyToClipboard },
        { "GetClipboardText", &taskGetClipboardText },
        { "GetSampoVersion", &taskGetSampoVersion },
        { "GetCurrentMod", &taskGetCurrentMod },
        { "LoadWandFromFile", &taskLoadWandFromFile },
        { "ExportWand", &taskExportWand }
    };
    const ApiFunction globalFunctions[] = {
        { "EntityGetChild", &entityGetChild },
        { "GenomeGetHerdId", &genomeGetHerdId },
        { "SetWorldTime", &setWorldTime },
        { "GetWorldTime", &getWorldTime },
        { "GetWorldSeed", &getWorldSeed },
        { "SetWorldSeed", &setWorldSeed },
        { "GetGoodSeed", &getGoodSeed },
        { "GetBadSeed", &getBadSeed },
        { "SpawnFlask", &spawnFlask },
        { "SpawnPerk", &spawnPerk },
        { "CreateWand", &createWand },
        { "EditWand", &editWandGlobal },
        { "AddSpellToWand", &addSpellToWand },
        { "EntityGetComponentValue", &entityGetComponentValue },
        { "EntitySetComponentValue", &entitySetComponentValue },
        { "EntityGetComponentObjectValue", &entityGetComponentObjectValue },
        { "EntitySetComponentObjectValue", &entitySetComponentObjectValue },
        { "EntityHasComponent", &entityHasComponent },
        { "EntityRequireComponent", &entityRequireComponent }
    };

    const std::size_t inputCount = sizeof(inputFunctions) / sizeof(inputFunctions[0]);
    const std::size_t playerCount = sizeof(playerFunctions) / sizeof(playerFunctions[0]);
    const std::size_t taskCount = sizeof(taskFunctions) / sizeof(taskFunctions[0]);
    const std::size_t globalCount = sizeof(globalFunctions) / sizeof(globalFunctions[0]);
    return loadTable(state, "input", inputFunctions, inputCount) && loadTable(state, "player", playerFunctions, playerCount) && loadTable(state, "task", taskFunctions, taskCount) && loadGlobals(state, globalFunctions, globalCount);
}
