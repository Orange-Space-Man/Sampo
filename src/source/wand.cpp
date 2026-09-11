#include "wand.h"

#include "debug.h"
#include "log.h"
#include "lua51.h"

#include <windows.h>
#include <commdlg.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
    struct NumberValue {
        double value = 0.0;
        bool found = false;
    };

    struct BooleanValue {
        bool value = false;
        bool found = false;
    };

    struct WandSpell {
        std::string id;
        int slotX = 0;
        int slotY = 0;
        bool permanent = false;
    };

    struct WandData {
        std::string name;
        std::string sprite;
        NumberValue manaMax;
        NumberValue manaChargeSpeed;
        NumberValue reloadTimeFrames;
        NumberValue recoil;
        NumberValue castDelay;
        NumberValue spread;
        NumberValue speedMultiplier;
        NumberValue actionsPerRound;
        NumberValue reloadTime;
        NumberValue capacity;
        NumberValue offsetX;
        NumberValue offsetY;
        NumberValue shootOffsetX;
        NumberValue shootOffsetY;
        BooleanValue shuffle;
        std::vector<std::string> spells;
        std::vector<std::string> alwaysCast;
    };

    enum class Section {
        none,
        wand,
        spells,
        alwaysCast
    };

    std::filesystem::path p_noitaPath;
    std::filesystem::path p_wandPath;
    std::filesystem::path p_exportPath;
    std::filesystem::path p_pendingLoad;
    std::filesystem::path p_pendingExport;
    std::mutex p_pendingMutex;
    bool p_initialized = false;

    std::string trim(const std::string& value) {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
            ++first;
        }

        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
            --last;
        }
        return value.substr(first, last - first);
    }

    std::string lower(std::string value) {
        for (char& character : value) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return value;
    }

    std::string upper(std::string value) {
        for (char& character : value) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return value;
    }

    std::string textValue(const std::string& value) {
        if (value.size() >= 2) {
            const char first = value.front();
            const char last = value.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                return value.substr(1, value.size() - 2);
            }
        }
        return value;
    }

    bool readNumber(const std::string& text, double& value) {
        errno = 0;
        char* end = nullptr;
        value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || errno == ERANGE || !std::isfinite(value)) {
            return false;
        }
        while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
            ++end;
        }
        return *end == '\0';
    }

    bool setNumber(NumberValue& field, const std::string& text, const char* name, std::size_t line, std::string& error) {
        double value = 0.0;
        if (!readNumber(text, value)) {
            error = "Line " + std::to_string(line) + ": " + name + " must be a number";
            return false;
        }
        field.value = value;
        field.found = true;
        return true;
    }

    bool setBoolean(BooleanValue& field, const std::string& text, const char* name, std::size_t line, std::string& error) {
        const std::string value = lower(text);
        if (value == "true" || value == "yes" || value == "1") {
            field.value = true;
        } else if (value == "false" || value == "no" || value == "0") {
            field.value = false;
        } else {
            error = "Line " + std::to_string(line) + ": " + name + " must be true or false";
            return false;
        }
        field.found = true;
        return true;
    }

    bool readWandValue(WandData& data, const std::string& name, const std::string& value, std::size_t line, std::string& error) {
        if (name == "name" || name == "ui_name") {
            data.name = textValue(value);
            return true;
        }
        if (name == "sprite" || name == "sprite_file" || name == "image" || name == "image_file" || name == "graphic") {
            data.sprite = textValue(value);
            return true;
        }
        if (name == "mana_max") {
            return setNumber(data.manaMax, value, "mana_max", line, error);
        }
        if (name == "mana_charge_speed") {
            return setNumber(data.manaChargeSpeed, value, "mana_charge_speed", line, error);
        }
        if (name == "reload_time_frames") {
            return setNumber(data.reloadTimeFrames, value, "reload_time_frames", line, error);
        }
        if (name == "item_recoil_max" || name == "recoil") {
            return setNumber(data.recoil, value, "recoil", line, error);
        }
        if (name == "cast_delay") {
            return setNumber(data.castDelay, value, "cast_delay", line, error);
        }
        if (name == "spread" || name == "spread_degrees") {
            return setNumber(data.spread, value, "spread_degrees", line, error);
        }
        if (name == "speed_multiplier") {
            return setNumber(data.speedMultiplier, value, "speed_multiplier", line, error);
        }
        if (name == "actions_per_round" || name == "spells_per_cast") {
            return setNumber(data.actionsPerRound, value, "actions_per_round", line, error);
        }
        if (name == "reload_time") {
            return setNumber(data.reloadTime, value, "reload_time", line, error);
        }
        if (name == "deck_capacity" || name == "capacity") {
            return setNumber(data.capacity, value, "capacity", line, error);
        }
        if (name == "offset_x") {
            return setNumber(data.offsetX, value, "offset_x", line, error);
        }
        if (name == "offset_y") {
            return setNumber(data.offsetY, value, "offset_y", line, error);
        }
        if (name == "shoot_offset_x" || name == "muzzle_offset_x") {
            return setNumber(data.shootOffsetX, value, "shoot_offset_x", line, error);
        }
        if (name == "shoot_offset_y" || name == "muzzle_offset_y") {
            return setNumber(data.shootOffsetY, value, "shoot_offset_y", line, error);
        }
        if (name == "shuffle" || name == "shuffle_deck_when_empty") {
            return setBoolean(data.shuffle, value, "shuffle", line, error);
        }

        error = "Line " + std::to_string(line) + ": unknown wand value " + name;
        return false;
    }

    bool readWand(const std::filesystem::path& path, WandData& data, std::string& error) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            error = "Could not open " + path.string();
            return false;
        }

        Section section = Section::none;
        std::string lineText;
        std::size_t lineNumber = 0;
        while (std::getline(file, lineText)) {
            ++lineNumber;
            if (lineNumber == 1 && lineText.size() >= 3 && static_cast<unsigned char>(lineText[0]) == 0xEF && static_cast<unsigned char>(lineText[1]) == 0xBB && static_cast<unsigned char>(lineText[2]) == 0xBF) {
                lineText.erase(0, 3);
            }

            const std::string line = trim(lineText);
            if (line.empty() || line.front() == '#' || line.front() == ';') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                const std::string name = lower(trim(line.substr(1, line.size() - 2)));
                if (name == "wand") {
                    section = Section::wand;
                } else if (name == "spells") {
                    section = Section::spells;
                } else if (name == "always_cast" || name == "always cast") {
                    section = Section::alwaysCast;
                } else {
                    error = "Line " + std::to_string(lineNumber) + ": unknown section " + name;
                    return false;
                }
                continue;
            }

            if (section == Section::spells || section == Section::alwaysCast) {
                const std::string spell = upper(textValue(line));
                if (spell.empty()) {
                    error = "Line " + std::to_string(lineNumber) + ": spell ID is empty";
                    return false;
                }
                if (section == Section::spells) {
                    data.spells.push_back(spell);
                } else {
                    data.alwaysCast.push_back(spell);
                }
                continue;
            }
            if (section != Section::wand) {
                error = "Line " + std::to_string(lineNumber) + ": add a section before values";
                return false;
            }

            const std::size_t equals = line.find('=');
            if (equals == std::string::npos) {
                error = "Line " + std::to_string(lineNumber) + ": expected name = value";
                return false;
            }
            const std::string name = lower(trim(line.substr(0, equals)));
            const std::string value = trim(line.substr(equals + 1));
            if (name.empty() || value.empty()) {
                error = "Line " + std::to_string(lineNumber) + ": expected name = value";
                return false;
            }
            if (!readWandValue(data, name, value, lineNumber, error)) {
                return false;
            }
        }

        if (data.spells.empty() && data.alwaysCast.empty()) {
            error = "The wand file does not contain any spells";
            return false;
        }
        if (data.shootOffsetX.found != data.shootOffsetY.found) {
            error = "shoot_offset_x and shoot_offset_y must be used together";
            return false;
        }
        return true;
    }

    bool getNoitaPath(std::filesystem::path& path) {
        wchar_t executable[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) {
            return false;
        }
        path = std::filesystem::path(executable).parent_path();
        return !path.empty();
    }

    bool utf8ToWide(const char* text, std::wstring& result) {
        const int length = static_cast<int>(std::strlen(text));
        if (length == 0) {
            return false;
        }
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0);
        if (required == 0) {
            return false;
        }
        result.resize(required);
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, result.data(), required) == required;
    }

    bool wideToUtf8(const std::wstring& text, std::string& result) {
        if (text.empty()) {
            result.clear();
            return true;
        }
        const int length = static_cast<int>(text.size());
        const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
        if (required == 0) {
            return false;
        }
        result.resize(required);
        return WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), required, nullptr, nullptr) == required;
    }

    bool findPath(const char* filename, std::filesystem::path& path, std::string& error) {
        if (filename == nullptr || *filename == '\0') {
            error = "A wand filename is required";
            return false;
        }

        std::wstring wideName;
        if (!utf8ToWide(filename, wideName)) {
            error = "The wand filename is not valid UTF-8";
            return false;
        }

        path = std::filesystem::path(wideName);
        if (path.extension().empty()) {
            path.replace_extension(L".wnd");
        }
        if (lower(path.extension().string()) != ".wnd") {
            error = "Wand files must use the .wnd extension";
            return false;
        }
        if (!path.is_absolute()) {
            if (path.has_parent_path()) {
                path = p_noitaPath / path;
            } else {
                path = p_wandPath / path;
            }
        }
        path = path.lexically_normal();
        return true;
    }

    bool findExportPath(const char* filename, std::filesystem::path& path, std::string& error) {
        if (filename == nullptr || *filename == '\0') {
            error = "A wand filename is required";
            return false;
        }

        std::wstring wideName;
        if (!utf8ToWide(filename, wideName)) {
            error = "The wand filename is not valid UTF-8";
            return false;
        }

        path = std::filesystem::path(wideName);
        if (path.is_absolute() || path.has_parent_path()) {
            error = "ExportWand only accepts a filename";
            return false;
        }
        if (path.extension().empty()) {
            path.replace_extension(L".wnd");
        }
        if (lower(path.extension().string()) != ".wnd") {
            error = "Wand files must use the .wnd extension";
            return false;
        }
        path = p_exportPath / path;
        return true;
    }

    void setNumberField(lua51::lua_State* state, int table, const char* name, const NumberValue& field) {
        if (!field.found) {
            return;
        }
        lua51::pushNumber(state, field.value);
        lua51::setField(state, table, name);
    }

    void setStringField(lua51::lua_State* state, int table, const char* name, const std::string& value) {
        if (value.empty()) {
            return;
        }
        lua51::pushString(state, value.c_str());
        lua51::setField(state, table, name);
    }

    void setBooleanField(lua51::lua_State* state, int table, const char* name, const BooleanValue& field) {
        if (!field.found) {
            return;
        }
        lua51::pushBoolean(state, field.value);
        lua51::setField(state, table, name);
    }

    bool callError(lua51::lua_State* state, const char* function, std::string& error) {
        const char* const message = lua51::toString(state, -1);
        error = std::string(function) + " failed";
        if (message != nullptr) {
            error.append(": ").append(message);
        }
        return false;
    }

    bool getComponent(lua51::lua_State* state, int entity, const char* type, int& component, std::string& error, const char* tag = nullptr) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "EntityGetFirstComponentIncludingDisabled");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "EntityGetFirstComponentIncludingDisabled is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(entity));
        lua51::pushString(state, type);
        int arguments = 2;
        if (tag != nullptr && *tag != '\0') {
            lua51::pushString(state, tag);
            arguments = 3;
        }
        if (lua51::pcall(state, arguments, 1, 0) != 0) {
            callError(state, "EntityGetFirstComponentIncludingDisabled", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            component = 0;
            return true;
        }

        component = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);
        return true;
    }

    bool getNumber(lua51::lua_State* state, int component, const char* field, double& value, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentGetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentGetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (lua51::pcall(state, 2, 1, 0) != 0) {
            callError(state, "ComponentGetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            error = std::string("Could not read ") + field;
            return false;
        }

        value = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool getString(lua51::lua_State* state, int component, const char* field, std::string& value, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentGetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentGetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (lua51::pcall(state, 2, 1, 0) != 0) {
            callError(state, "ComponentGetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeString) {
            lua51::setTop(state, top);
            error = std::string("Could not read ") + field;
            return false;
        }

        const char* const text = lua51::toString(state, -1);
        value = text == nullptr ? "" : text;
        lua51::setTop(state, top);
        return true;
    }

    bool getBoolean(lua51::lua_State* state, int component, const char* field, bool& value, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentGetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentGetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (lua51::pcall(state, 2, 1, 0) != 0) {
            callError(state, "ComponentGetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeBoolean) {
            lua51::setTop(state, top);
            error = std::string("Could not read ") + field;
            return false;
        }

        value = lua51::toBoolean(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool getVector(lua51::lua_State* state, int component, const char* field, double& x, double& y, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentGetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentGetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (lua51::pcall(state, 2, 2, 0) != 0) {
            callError(state, "ComponentGetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -2) != lua51::typeNumber || lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            error = std::string("Could not read ") + field;
            return false;
        }

        x = lua51::toNumber(state, -2);
        y = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool setVector(lua51::lua_State* state, int component, const char* field, double x, double y, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentSetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentSetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        lua51::pushNumber(state, x);
        lua51::pushNumber(state, y);
        if (lua51::pcall(state, 4, 0, 0) != 0) {
            callError(state, "ComponentSetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool getObjectNumber(lua51::lua_State* state, int component, const char* object, const char* field, double& value, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentObjectGetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentObjectGetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, object);
        lua51::pushString(state, field);
        if (lua51::pcall(state, 3, 1, 0) != 0) {
            callError(state, "ComponentObjectGetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            error = std::string("Could not read ") + object + "." + field;
            return false;
        }

        value = lua51::toNumber(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool getObjectBoolean(lua51::lua_State* state, int component, const char* object, const char* field, bool& value, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentObjectGetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentObjectGetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, object);
        lua51::pushString(state, field);
        if (lua51::pcall(state, 3, 1, 0) != 0) {
            callError(state, "ComponentObjectGetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeBoolean) {
            lua51::setTop(state, top);
            error = std::string("Could not read ") + object + "." + field;
            return false;
        }

        value = lua51::toBoolean(state, -1);
        lua51::setTop(state, top);
        return true;
    }

    bool setObjectNumber(lua51::lua_State* state, int component, const char* object, const char* field, double value, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ComponentObjectSetValue2");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "ComponentObjectSetValue2 is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, object);
        lua51::pushString(state, field);
        lua51::pushNumber(state, value);
        if (lua51::pcall(state, 4, 0, 0) != 0) {
            callError(state, "ComponentObjectSetValue2", error);
            lua51::setTop(state, top);
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool getChildren(lua51::lua_State* state, int entity, std::vector<int>& children, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "EntityGetAllChildren");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "EntityGetAllChildren is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(entity));
        if (lua51::pcall(state, 1, 1, 0) != 0) {
            callError(state, "EntityGetAllChildren", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) == lua51::typeTable) {
            const int table = lua51::getTop(state);
            for (int index = 1;; index++) {
                lua51::rawGetIndex(state, table, index);
                if (lua51::type(state, -1) == lua51::typeNil) {
                    lua51::pop(state, 1);
                    break;
                }
                if (lua51::type(state, -1) == lua51::typeNumber) {
                    children.push_back(static_cast<int>(lua51::toNumber(state, -1)));
                }
                lua51::pop(state, 1);
            }
        }
        lua51::setTop(state, top);
        return true;
    }

    bool readEntity(lua51::lua_State* state, int entity, WandData& data, std::string& error) {
        int ability = 0;
        if (!getComponent(state, entity, "AbilityComponent", ability, error)) {
            return false;
        }
        if (ability == 0) {
            error = "The entity is not a wand";
            return false;
        }

        if (!getString(state, ability, "ui_name", data.name, error) || !getString(state, ability, "sprite_file", data.sprite, error)) {
            return false;
        }
        if (!getNumber(state, ability, "mana_max", data.manaMax.value, error) || !getNumber(state, ability, "mana_charge_speed", data.manaChargeSpeed.value, error) || !getNumber(state, ability, "reload_time_frames", data.reloadTimeFrames.value, error) || !getNumber(state, ability, "item_recoil_max", data.recoil.value, error)) {
            return false;
        }
        if (!getObjectNumber(state, ability, "gunaction_config", "fire_rate_wait", data.castDelay.value, error) || !getObjectNumber(state, ability, "gunaction_config", "spread_degrees", data.spread.value, error) || !getObjectNumber(state, ability, "gunaction_config", "speed_multiplier", data.speedMultiplier.value, error)) {
            return false;
        }
        if (!getObjectNumber(state, ability, "gun_config", "actions_per_round", data.actionsPerRound.value, error) || !getObjectNumber(state, ability, "gun_config", "reload_time", data.reloadTime.value, error) || !getObjectNumber(state, ability, "gun_config", "deck_capacity", data.capacity.value, error) || !getObjectBoolean(state, ability, "gun_config", "shuffle_deck_when_empty", data.shuffle.value, error)) {
            return false;
        }
        data.manaMax.found = true;
        data.manaChargeSpeed.found = true;
        data.reloadTimeFrames.found = true;
        data.recoil.found = true;
        data.castDelay.found = true;
        data.spread.found = true;
        data.speedMultiplier.found = true;
        data.actionsPerRound.found = true;
        data.reloadTime.found = true;
        data.capacity.found = true;
        data.shuffle.found = true;

        int sprite = 0;
        if (!getComponent(state, entity, "SpriteComponent", sprite, error)) {
            return false;
        }
        if (sprite != 0) {
            double offsetX = 0.0;
            double offsetY = 0.0;
            if (!getNumber(state, sprite, "offset_x", offsetX, error) || !getNumber(state, sprite, "offset_y", offsetY, error)) {
                return false;
            }
            data.offsetX.value = offsetX;
            data.offsetX.found = true;
            data.offsetY.value = offsetY;
            data.offsetY.found = true;
            if (data.sprite.empty() && !getString(state, sprite, "image_file", data.sprite, error)) {
                return false;
            }
        }

        int shootPosition = 0;
        if (!getComponent(state, entity, "HotspotComponent", shootPosition, error, "shoot_pos")) {
            return false;
        }
        if (shootPosition != 0) {
            if (!getVector(state, shootPosition, "offset", data.shootOffsetX.value, data.shootOffsetY.value, error)) {
                return false;
            }
            data.shootOffsetX.found = true;
            data.shootOffsetY.found = true;
        }

        std::vector<int> children;
        if (!getChildren(state, entity, children, error)) {
            return false;
        }

        std::vector<WandSpell> spells;
        for (const int child : children) {
            int action = 0;
            int item = 0;
            if (!getComponent(state, child, "ItemActionComponent", action, error) || !getComponent(state, child, "ItemComponent", item, error)) {
                return false;
            }
            if (action == 0 || item == 0) {
                continue;
            }

            WandSpell spell;
            double slotX = 0.0;
            double slotY = 0.0;
            if (!getString(state, action, "action_id", spell.id, error) || !getBoolean(state, item, "permanently_attached", spell.permanent, error) || !getVector(state, item, "inventory_slot", slotX, slotY, error)) {
                return false;
            }
            spell.slotX = static_cast<int>(slotX);
            spell.slotY = static_cast<int>(slotY);
            spells.push_back(std::move(spell));
        }

        std::stable_sort(spells.begin(), spells.end(), [](const WandSpell& left, const WandSpell& right) {
            if (left.slotY != right.slotY) {
                return left.slotY < right.slotY;
            }
            return left.slotX < right.slotX;
        });
        for (const WandSpell& spell : spells) {
            if (spell.permanent) {
                data.alwaysCast.push_back(spell.id);
            } else {
                data.spells.push_back(spell.id);
            }
        }
        return true;
    }

    std::string numberText(double value) {
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
        return text.str();
    }

    bool writeWand(const std::filesystem::path& path, const WandData& data, std::string& error) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = "Could not open " + path.string();
            return false;
        }

        file << "[wand]\n";
        if (!data.name.empty()) {
            file << "name = " << data.name << "\n";
        }
        if (!data.sprite.empty()) {
            file << "image = " << data.sprite << "\n";
        }
        file << "shuffle = " << (data.shuffle.value ? "true" : "false") << "\n";
        file << "actions_per_round = " << numberText(data.actionsPerRound.value) << "\n";
        file << "cast_delay = " << numberText(data.castDelay.value) << "\n";
        file << "reload_time = " << numberText(data.reloadTime.value) << "\n";
        file << "mana_max = " << numberText(data.manaMax.value) << "\n";
        file << "mana_charge_speed = " << numberText(data.manaChargeSpeed.value) << "\n";
        file << "capacity = " << numberText(data.capacity.value) << "\n";
        file << "spread_degrees = " << numberText(data.spread.value) << "\n";
        file << "speed_multiplier = " << numberText(data.speedMultiplier.value) << "\n";
        file << "recoil = " << numberText(data.recoil.value) << "\n";
        file << "reload_time_frames = " << numberText(data.reloadTimeFrames.value) << "\n";
        if (data.offsetX.found) {
            file << "offset_x = " << numberText(data.offsetX.value) << "\n";
        }
        if (data.offsetY.found) {
            file << "offset_y = " << numberText(data.offsetY.value) << "\n";
        }
        if (data.shootOffsetX.found && data.shootOffsetY.found) {
            file << "shoot_offset_x = " << numberText(data.shootOffsetX.value) << "\n";
            file << "shoot_offset_y = " << numberText(data.shootOffsetY.value) << "\n";
        }
        if (!data.alwaysCast.empty()) {
            file << "\n[always_cast]\n";
            for (const std::string& spell : data.alwaysCast) {
                file << spell << "\n";
            }
        }
        if (!data.spells.empty()) {
            file << "\n[spells]\n";
            for (const std::string& spell : data.spells) {
                file << spell << "\n";
            }
        }
        if (!file.good()) {
            error = "Could not write " + path.string();
            return false;
        }
        return true;
    }

    bool createWand(lua51::lua_State* state, const WandData& data, int& wandEntity, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "CreateWand");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "CreateWand is unavailable in this Lua state";
            return false;
        }

        lua51::createTable(state, 0, 7);
        const int ability = lua51::getTop(state);
        setNumberField(state, ability, "mana_max", data.manaMax);
        setNumberField(state, ability, "mana_charge_speed", data.manaChargeSpeed);
        setNumberField(state, ability, "reload_time_frames", data.reloadTimeFrames);
        setNumberField(state, ability, "item_recoil_max", data.recoil);
        setNumberField(state, ability, "cast_delay", data.castDelay);
        setNumberField(state, ability, "spread_degrees", data.spread);
        setNumberField(state, ability, "speed_multiplier", data.speedMultiplier);

        lua51::createTable(state, 0, 4);
        const int gun = lua51::getTop(state);
        setNumberField(state, gun, "actions_per_round", data.actionsPerRound);
        setNumberField(state, gun, "reload_time", data.reloadTime);
        setNumberField(state, gun, "deck_capacity", data.capacity);
        setBooleanField(state, gun, "shuffle_deck_when_empty", data.shuffle);

        lua51::createTable(state, 0, 4);
        const int sprite = lua51::getTop(state);
        setStringField(state, sprite, "ui_name", data.name);
        setStringField(state, sprite, "sprite", data.sprite);
        setNumberField(state, sprite, "offset_x", data.offsetX);
        setNumberField(state, sprite, "offset_y", data.offsetY);

        if (lua51::pcall(state, 3, 1, 0) != 0) {
            callError(state, "CreateWand", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            error = "CreateWand did not return a wand entity";
            return false;
        }
        wandEntity = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);

        if (data.shootOffsetX.found && data.shootOffsetY.found) {
            int shootPosition = 0;
            if (!getComponent(state, wandEntity, "HotspotComponent", shootPosition, error, "shoot_pos")) {
                return false;
            }
            if (shootPosition == 0) {
                error = "The wand has no shoot_pos HotspotComponent";
                return false;
            }
            if (!setVector(state, shootPosition, "offset", data.shootOffsetX.value, data.shootOffsetY.value, error)) {
                return false;
            }
        }
        return true;
    }

    bool clearWand(lua51::lua_State* state, int wandEntity, std::string& error) {
        const int top = lua51::getTop(state);
        std::vector<int> children;
        if (!getChildren(state, wandEntity, children, error)) {
            return false;
        }

        for (const int child : children) {
            int action = 0;
            if (!getComponent(state, child, "ItemActionComponent", action, error)) {
                return false;
            }
            if (action == 0) {
                continue;
            }

            lua51::getGlobal(state, "EntityRemoveFromParent");
            if (lua51::type(state, -1) != lua51::typeFunction) {
                lua51::setTop(state, top);
                error = "EntityRemoveFromParent is unavailable in this Lua state";
                return false;
            }
            lua51::pushNumber(state, static_cast<double>(child));
            if (lua51::pcall(state, 1, 0, 0) != 0) {
                callError(state, "EntityRemoveFromParent", error);
                lua51::setTop(state, top);
                return false;
            }

            lua51::getGlobal(state, "EntityKill");
            if (lua51::type(state, -1) != lua51::typeFunction) {
                lua51::setTop(state, top);
                error = "EntityKill is unavailable in this Lua state";
                return false;
            }
            lua51::pushNumber(state, static_cast<double>(child));
            if (lua51::pcall(state, 1, 0, 0) != 0) {
                callError(state, "EntityKill", error);
                lua51::setTop(state, top);
                return false;
            }
        }
        lua51::setTop(state, top);
        return true;
    }

    bool addSpell(lua51::lua_State* state, int wandEntity, const std::string& spell, bool permanent, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "AddSpellToWand");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "AddSpellToWand is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(wandEntity));
        lua51::pushString(state, spell.c_str());
        lua51::pushBoolean(state, permanent);
        if (lua51::pcall(state, 3, 1, 0) != 0) {
            callError(state, "AddSpellToWand", error);
            lua51::setTop(state, top);
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool giveWand(lua51::lua_State* state, int wandEntity, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "player");
        if (lua51::type(state, -1) != lua51::typeTable) {
            lua51::setTop(state, top);
            error = "player is unavailable in this Lua state";
            return false;
        }
        lua51::getField(state, -1, "GiveItem");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "player.GiveItem is unavailable in this Lua state";
            return false;
        }

        lua51::pushNumber(state, static_cast<double>(wandEntity));
        if (lua51::pcall(state, 1, 1, 0) != 0) {
            callError(state, "player.GiveItem", error);
            lua51::setTop(state, top);
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool loadWand(lua51::lua_State* state, const std::filesystem::path& path, int& entity, std::string& error) {
        WandData data;
        if (!readWand(path, data, error)) {
            return false;
        }
        if (!createWand(state, data, entity, error)) {
            return false;
        }
        if (!clearWand(state, entity, error)) {
            return false;
        }

        for (const std::string& spell : data.alwaysCast) {
            if (!addSpell(state, entity, spell, true, error)) {
                return false;
            }
        }
        for (const std::string& spell : data.spells) {
            if (!addSpell(state, entity, spell, false, error)) {
                return false;
            }
        }

        if (data.capacity.found) {
            int ability = 0;
            if (!getComponent(state, entity, "AbilityComponent", ability, error) || ability == 0) {
                error = "The loaded wand has no AbilityComponent";
                return false;
            }
            if (!setObjectNumber(state, ability, "gun_config", "deck_capacity", data.capacity.value, error)) {
                return false;
            }
        }
        return giveWand(state, entity, error);
    }

    std::vector<std::filesystem::path> getWandFiles() {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(p_wandPath, error)) {
            if (entry.is_regular_file(error) && lower(entry.path().extension().string()) == ".wnd") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    void queueWand(const std::filesystem::path& path) {
        std::scoped_lock lock(p_pendingMutex);
        p_pendingLoad = path;
    }

    void queueExport(const std::filesystem::path& path) {
        std::scoped_lock lock(p_pendingMutex);
        p_pendingExport = path;
    }

    void browseWands() {
        wchar_t filename[32768]{};
        const std::wstring directory = p_wandPath.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"Wand files (*.wnd)\0*.wnd\0All files (*.*)\0*.*\0";
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
        dialog.lpstrInitialDir = directory.c_str();
        dialog.lpstrDefExt = L"wnd";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) {
            queueWand(filename);
        }
    }

    void browseExport() {
        wchar_t filename[32768] = L"wand.wnd";
        const std::wstring directory = p_exportPath.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"Wand files (*.wnd)\0*.wnd\0\0";
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
        dialog.lpstrInitialDir = directory.c_str();
        dialog.lpstrDefExt = L"wnd";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&dialog)) {
            queueExport(p_exportPath / std::filesystem::path(filename).filename());
        }
    }

    bool getActiveWand(lua51::lua_State* state, int& entity, std::string& error) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "player");
        if (lua51::type(state, -1) != lua51::typeTable) {
            lua51::setTop(state, top);
            error = "player is unavailable in this Lua state";
            return false;
        }
        lua51::getField(state, -1, "GetActiveWand");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            error = "player.GetActiveWand is unavailable in this Lua state";
            return false;
        }
        if (lua51::pcall(state, 0, 1, 0) != 0) {
            callError(state, "player.GetActiveWand", error);
            lua51::setTop(state, top);
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            error = "No wand is currently held";
            return false;
        }

        entity = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);
        return true;
    }
}

bool wand::init() {
    if (p_initialized) {
        return true;
    }
    if (!getNoitaPath(p_noitaPath)) {
        sampo::log::error("Could not find Noita's path for wand files.");
        return false;
    }

    p_wandPath = p_noitaPath / "sampo" / "wands" / "premade";
    p_exportPath = p_noitaPath / "sampo" / "wands" / "exported";
    std::error_code error;
    std::filesystem::create_directories(p_wandPath, error);
    if (error) {
        sampo::log::error("Could not create wand directory: /arrow Path: %s /arrow Error: %s", p_wandPath.string().c_str(), error.message().c_str());
        return false;
    }
    std::filesystem::create_directories(p_exportPath, error);
    if (error) {
        sampo::log::error("Could not create wand directory: /arrow Path: %s /arrow Error: %s", p_exportPath.string().c_str(), error.message().c_str());
        return false;
    }

    p_initialized = true;
    return true;
}

bool wand::load(lua51::lua_State* state, const char* filename, int& entity, std::string& error) {
    if (!p_initialized && !init()) {
        error = "The wand file system is not initialized";
        return false;
    }
    if (state == nullptr || !lua51::ready()) {
        error = "Lua is not ready";
        return false;
    }

    std::filesystem::path path;
    if (!findPath(filename, path, error)) {
        return false;
    }
    return loadWand(state, path, entity, error);
}

bool wand::save(lua51::lua_State* state, int entity, const char* filename, std::string& path, std::string& error) {
    if (!p_initialized && !init()) {
        error = "The wand file system is not initialized";
        return false;
    }
    if (state == nullptr || !lua51::ready()) {
        error = "Lua is not ready";
        return false;
    }

    std::filesystem::path file;
    if (!findExportPath(filename, file, error)) {
        return false;
    }

    WandData data;
    if (!readEntity(state, entity, data, error) || !writeWand(file, data, error)) {
        return false;
    }
    if (!wideToUtf8(file.wstring(), path)) {
        path = file.string();
    }
    return true;
}

void wand::update(lua51::lua_State* state) {
    std::filesystem::path loadPath;
    std::filesystem::path exportPath;
    {
        std::scoped_lock lock(p_pendingMutex);
        if (p_pendingLoad.empty() && p_pendingExport.empty()) {
            return;
        }
        loadPath = std::move(p_pendingLoad);
        exportPath = std::move(p_pendingExport);
        p_pendingLoad.clear();
        p_pendingExport.clear();
    }

    if (!loadPath.empty()) {
        int entity = 0;
        std::string error;
        if (!loadWand(state, loadPath, entity, error)) {
            sampo::log::error("Could not load wand: /arrow File: %s /arrow Error: %s", loadPath.string().c_str(), error.c_str());
        } else {
            sampo::log::write("Loaded wand: /arrow File: %s /arrow Entity: %d", loadPath.string().c_str(), entity);
        }
    }

    if (!exportPath.empty()) {
        int entity = 0;
        std::string error;
        WandData data;
        if (!getActiveWand(state, entity, error) || !readEntity(state, entity, data, error) || !writeWand(exportPath, data, error)) {
            sampo::log::error("Could not export wand: /arrow File: %s /arrow Error: %s", exportPath.string().c_str(), error.c_str());
        } else {
            sampo::log::write("Exported wand: /arrow File: %s /arrow Entity: %d", exportPath.string().c_str(), entity);
        }
    }
}

void wand::drawMenu() {
    if (!ImGui::BeginMenu("File")) {
        return;
    }

    if (ImGui::BeginMenu("Load Wand..")) {
        const std::vector<std::filesystem::path> files = getWandFiles();
        if (files.empty()) {
            ImGui::TextDisabled("No premade wands");
        } else {
            for (const std::filesystem::path& path : files) {
                std::string name;
                if (!wideToUtf8(path.filename().wstring(), name)) {
                    name = path.filename().string();
                }
                if (ImGui::MenuItem(name.c_str())) {
                    queueWand(path);
                }
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Browse..")) {
            browseWands();
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Export Wand..")) {
        browseExport();
    }

    debug::drawMenu();

    ImGui::EndMenu();
}
