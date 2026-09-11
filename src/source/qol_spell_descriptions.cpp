#include "qol_spell_descriptions.h"

#include "log.h"
#include "lua51.h"
#include "settings.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
    constexpr int p_updateRate = 15;
    constexpr const char* p_markerName = "sampo_spell_descriptions";
    constexpr const char* p_oldMarkerName = "spelldesc_init";

    struct SpellRecord {
        int item = 0;
        int marker = 0;
        std::string description;
        bool display = false;
        bool changed = false;
    };

    int p_lastFrame = -1;
    bool p_enabled = false;
    bool p_actionsReady = false;
    bool p_errorShown = false;
    std::unordered_map<std::string, std::string> p_descriptions;
    std::unordered_map<int, SpellRecord> p_spells;
    std::unordered_set<int> p_ignored;

    void showError(lua51::lua_State* state, const char* function) {
        if (p_errorShown) {
            return;
        }

        const char* message = lua51::toString(state, -1);
        if (message == nullptr) {
            message = "unknown error";
        }
        sampo::log::error("Spell descriptions failed: /arrow Function: %s /arrow Error: %s", function, message);
        p_errorShown = true;
    }

    bool beginCall(lua51::lua_State* state, const char* function, int top) {
        lua51::getGlobal(state, function);
        if (lua51::type(state, -1) == lua51::typeFunction) {
            return true;
        }
        lua51::setTop(state, top);
        return false;
    }

    bool finishCall(lua51::lua_State* state, const char* function, int arguments, int results, int top) {
        if (lua51::pcall(state, arguments, results, 0) == 0) {
            return true;
        }
        showError(state, function);
        lua51::setTop(state, top);
        return false;
    }

    bool getFrame(lua51::lua_State* state, int& frame) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "GameGetFrameNum", top)) {
            return false;
        }
        if (!finishCall(state, "GameGetFrameNum", 0, 1, top)) {
            return false;
        }
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::setTop(state, top);
            return false;
        }
        frame = static_cast<int>(lua51::toNumber(state, -1));
        lua51::setTop(state, top);
        return true;
    }

    bool loadGunActions(lua51::lua_State* state) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "dofile_once", top)) {
            return false;
        }
        lua51::pushString(state, "data/scripts/gun/gun.lua");
        if (!finishCall(state, "dofile_once", 1, 0, top)) {
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool readField(lua51::lua_State* state, int table, const char* field, std::string& value) {
        lua51::getField(state, table, field);
        if (lua51::type(state, -1) != lua51::typeString) {
            lua51::pop(state, 1);
            return false;
        }
        const char* text = lua51::toString(state, -1);
        if (text == nullptr) {
            lua51::pop(state, 1);
            return false;
        }
        value = text;
        lua51::pop(state, 1);
        return true;
    }

    void cleanDescription(std::string& description) {
        for (char& character : description) {
            if (character == '\r' || character == '\n' || character == '\t') {
                character = ' ';
            }
        }
    }

    bool loadDescriptions(lua51::lua_State* state) {
        if (p_actionsReady) {
            return true;
        }
        if (!loadGunActions(state)) {
            return false;
        }

        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "actions");
        if (lua51::type(state, -1) != lua51::typeTable) {
            lua51::setTop(state, top);
            return false;
        }

        const int actions = lua51::getTop(state);
        for (int index = 1;; index++) {
            lua51::rawGetIndex(state, actions, index);
            if (lua51::type(state, -1) == lua51::typeNil) {
                lua51::pop(state, 1);
                break;
            }
            if (lua51::type(state, -1) == lua51::typeTable) {
                const int action = lua51::getTop(state);
                std::string id;
                std::string description;
                if (readField(state, action, "id", id) && readField(state, action, "description", description) && !id.empty() && !description.empty()) {
                    cleanDescription(description);
                    p_descriptions[id] = std::move(description);
                }
            }
            lua51::pop(state, 1);
        }

        lua51::setTop(state, top);
        p_actionsReady = !p_descriptions.empty();
        return p_actionsReady;
    }

    bool getEntities(lua51::lua_State* state, std::vector<int>& entities) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityGetWithTag", top)) {
            return false;
        }
        lua51::pushString(state, "card_action");
        if (!finishCall(state, "EntityGetWithTag", 1, 1, top)) {
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

    bool getComponents(lua51::lua_State* state, int entity, const char* type, std::vector<int>& components) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityGetComponentIncludingDisabled", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(entity));
        lua51::pushString(state, type);
        if (!finishCall(state, "EntityGetComponentIncludingDisabled", 2, 1, top)) {
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
                components.push_back(static_cast<int>(lua51::toNumber(state, -1)));
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
        if (!finishCall(state, "EntityGetFirstComponentIncludingDisabled", 2, 1, top)) {
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

    bool getString(lua51::lua_State* state, int component, const char* field, std::string& value) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentGetValue2", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (!finishCall(state, "ComponentGetValue2", 2, 1, top)) {
            return false;
        }
        const char* text = lua51::toString(state, -1);
        if (text == nullptr) {
            lua51::setTop(state, top);
            return false;
        }
        value = text;
        lua51::setTop(state, top);
        return true;
    }

    bool getBool(lua51::lua_State* state, int component, const char* field, bool& value) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentGetValue2", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        if (!finishCall(state, "ComponentGetValue2", 2, 1, top)) {
            return false;
        }
        if (lua51::type(state, -1) == lua51::typeBoolean) {
            value = lua51::toBoolean(state, -1);
        } else if (lua51::type(state, -1) == lua51::typeNumber) {
            value = lua51::toNumber(state, -1) != 0.0;
        } else {
            lua51::setTop(state, top);
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool setString(lua51::lua_State* state, int component, const char* field, const std::string& value) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentSetValue2", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        lua51::pushString(state, value.c_str());
        if (!finishCall(state, "ComponentSetValue2", 3, 0, top)) {
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool setBool(lua51::lua_State* state, int component, const char* field, bool value) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "ComponentSetValue2", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(component));
        lua51::pushString(state, field);
        lua51::pushBoolean(state, value);
        if (!finishCall(state, "ComponentSetValue2", 3, 0, top)) {
            return false;
        }
        lua51::setTop(state, top);
        return true;
    }

    bool hasMarker(lua51::lua_State* state, int entity) {
        std::vector<int> components;
        if (!getComponents(state, entity, "VariableStorageComponent", components)) {
            return false;
        }
        for (const int component : components) {
            std::string name;
            if (getString(state, component, "name", name) && (name == p_markerName || name == p_oldMarkerName)) {
                return true;
            }
        }
        return false;
    }

    int addMarker(lua51::lua_State* state, int entity) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityAddComponent2", top)) {
            return 0;
        }
        lua51::pushNumber(state, static_cast<double>(entity));
        lua51::pushString(state, "VariableStorageComponent");
        lua51::createTable(state, 0, 1);
        lua51::pushString(state, p_markerName);
        lua51::setField(state, -2, "name");
        if (!finishCall(state, "EntityAddComponent2", 3, 1, top)) {
            return 0;
        }
        int marker = 0;
        if (lua51::type(state, -1) == lua51::typeNumber) {
            marker = static_cast<int>(lua51::toNumber(state, -1));
        }
        lua51::setTop(state, top);
        return marker;
    }

    void removeMarker(lua51::lua_State* state, int entity, int marker) {
        if (marker == 0) {
            return;
        }
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityRemoveComponent", top)) {
            return;
        }
        lua51::pushNumber(state, static_cast<double>(entity));
        lua51::pushNumber(state, static_cast<double>(marker));
        finishCall(state, "EntityRemoveComponent", 2, 0, top);
        lua51::setTop(state, top);
    }

    bool isAlive(lua51::lua_State* state, int entity) {
        const int top = lua51::getTop(state);
        if (!beginCall(state, "EntityGetIsAlive", top)) {
            return false;
        }
        lua51::pushNumber(state, static_cast<double>(entity));
        if (!finishCall(state, "EntityGetIsAlive", 1, 1, top)) {
            return false;
        }
        bool alive = false;
        if (lua51::type(state, -1) == lua51::typeBoolean) {
            alive = lua51::toBoolean(state, -1);
        }
        lua51::setTop(state, top);
        return alive;
    }

    void applyDescription(lua51::lua_State* state, int entity) {
        if (p_spells.contains(entity) || p_ignored.contains(entity)) {
            return;
        }
        if (hasMarker(state, entity)) {
            p_ignored.insert(entity);
            return;
        }

        SpellRecord record;
        int actionComponent = 0;
        int itemComponent = 0;
        if (getFirstComponent(state, entity, "ItemActionComponent", actionComponent) && getFirstComponent(state, entity, "ItemComponent", itemComponent)) {
            std::string action;
            if (getString(state, actionComponent, "action_id", action)) {
                const auto description = p_descriptions.find(action);
                if (description != p_descriptions.end() && getString(state, itemComponent, "ui_description", record.description) && getBool(state, itemComponent, "ui_display_description_on_pick_up_hint", record.display)) {
                    record.item = itemComponent;
                    record.changed = record.description != description->second || !record.display;
                    if (record.changed) {
                        if (!setString(state, itemComponent, "ui_description", description->second) || !setBool(state, itemComponent, "ui_display_description_on_pick_up_hint", true)) {
                            setString(state, itemComponent, "ui_description", record.description);
                            setBool(state, itemComponent, "ui_display_description_on_pick_up_hint", record.display);
                            record.item = 0;
                            record.changed = false;
                        }
                    }
                }
            }
        }
        record.marker = addMarker(state, entity);
        p_spells[entity] = std::move(record);
    }

    void restoreDescriptions(lua51::lua_State* state) {
        for (const auto& [entity, record] : p_spells) {
            if (!isAlive(state, entity)) {
                continue;
            }
            if (record.changed && record.item != 0) {
                setString(state, record.item, "ui_description", record.description);
                setBool(state, record.item, "ui_display_description_on_pick_up_hint", record.display);
            }
            removeMarker(state, entity, record.marker);
        }
        p_spells.clear();
        p_ignored.clear();
    }

    void clearWorld() {
        p_descriptions.clear();
        p_spells.clear();
        p_ignored.clear();
        p_actionsReady = false;
        p_errorShown = false;
    }
}

void spell_descriptions::update(lua51::lua_State* state) {
    if (state == nullptr || !lua51::ready()) {
        return;
    }
    const bool enabled = settings::spellDescriptions();
    if (!enabled) {
        if (p_enabled) {
            restoreDescriptions(state);
            p_enabled = false;
        }
        return;
    }

    int frame = 0;
    if (!getFrame(state, frame)) {
        return;
    }
    if (frame < p_lastFrame) {
        clearWorld();
    }
    if (frame == p_lastFrame) {
        return;
    }
    p_lastFrame = frame;
    p_enabled = true;
    if (frame % p_updateRate != 0) {
        return;
    }
    if (!loadDescriptions(state)) {
        return;
    }

    std::vector<int> entities;
    if (!getEntities(state, entities)) {
        return;
    }
    for (const int entity : entities) {
        applyDescription(state, entity);
    }
}
