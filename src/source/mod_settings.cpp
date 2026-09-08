#include "mod_settings.h"

#include "log.h"
#include "lua51.h"
#include "settings.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace {
    enum class ValueType {
        Nil,
        Boolean,
        Number,
        String
    };

    struct Value {
        ValueType type = ValueType::Nil;
        bool boolean = false;
        double number = 0.0;
        std::string text;
    };

    struct Page {
        lua51::lua_State* state = nullptr;
        std::string id;
        std::string name;
        std::string error;
        int settings = -1;
        bool requested = false;
        bool visible = false;
    };

    std::mutex p_mutex;
    std::vector<std::string> p_secretPaths;
    Page p_page;
    thread_local bool p_loadingSettings = false;

    std::string normalize(std::string text) {
        for (char& character : text) {
            if (character == '\\') {
                character = '/';
            } else {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
        }
        if (!text.empty() && text.front() == '@') {
            text.erase(text.begin());
        }
        return text;
    }

    bool endsWith(const std::string& text, const std::string& ending) {
        if (ending.size() > text.size()) {
            return false;
        }
        return text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
    }

    int absoluteIndex(lua51::lua_State* state, int index) {
        if (index > 0 || index <= lua51::registryIndex) {
            return index;
        }
        return lua51::getTop(state) + index + 1;
    }

    std::string stackText(lua51::lua_State* state, int index) {
        std::size_t length = 0;
        const char* const text = lua51::toString(state, index, &length);
        if (text == nullptr) {
            return {};
        }
        return std::string(text, length);
    }

    std::string fieldText(lua51::lua_State* state, int index, const char* name) {
        const int table = absoluteIndex(state, index);
        lua51::getField(state, table, name);
        std::string value;
        if (lua51::type(state, -1) == lua51::typeString) {
            value = stackText(state, -1);
        }
        lua51::pop(state, 1);
        return value;
    }

    bool fieldBoolean(lua51::lua_State* state, int index, const char* name, bool fallback) {
        const int table = absoluteIndex(state, index);
        lua51::getField(state, table, name);
        bool value = fallback;
        if (lua51::type(state, -1) == lua51::typeBoolean) {
            value = lua51::toBoolean(state, -1);
        }
        lua51::pop(state, 1);
        return value;
    }

    bool fieldNumber(lua51::lua_State* state, int index, const char* name, double& value) {
        const int table = absoluteIndex(state, index);
        lua51::getField(state, table, name);
        const bool found = lua51::type(state, -1) == lua51::typeNumber;
        if (found) {
            value = lua51::toNumber(state, -1);
        }
        lua51::pop(state, 1);
        return found;
    }

    Value stackValue(lua51::lua_State* state, int index) {
        Value value;
        const int type = lua51::type(state, index);
        if (type == lua51::typeBoolean) {
            value.type = ValueType::Boolean;
            value.boolean = lua51::toBoolean(state, index);
        } else if (type == lua51::typeNumber) {
            value.type = ValueType::Number;
            value.number = lua51::toNumber(state, index);
        } else if (type == lua51::typeString) {
            value.type = ValueType::String;
            value.text = stackText(state, index);
        }
        return value;
    }

    Value fieldValue(lua51::lua_State* state, int index, const char* name) {
        const int table = absoluteIndex(state, index);
        lua51::getField(state, table, name);
        const Value value = stackValue(state, -1);
        lua51::pop(state, 1);
        return value;
    }

    void pushValue(lua51::lua_State* state, const Value& value) {
        if (value.type == ValueType::Boolean) {
            lua51::pushBoolean(state, value.boolean);
        } else if (value.type == ValueType::Number) {
            lua51::pushNumber(state, value.number);
        } else if (value.type == ValueType::String) {
            lua51::pushString(state, value.text.c_str());
        } else {
            lua51::pushNil(state);
        }
    }

    std::string luaError(lua51::lua_State* state) {
        const std::string error = stackText(state, -1);
        if (error.empty()) {
            return "Unknown Lua error";
        }
        return error;
    }

    int saveGlobal(lua51::lua_State* state, const char* name) {
        lua51::getGlobal(state, name);
        return lua51::reference(state);
    }

    void restoreGlobal(lua51::lua_State* state, const char* name, int reference) {
        if (reference < 0) {
            lua51::pushNil(state);
        } else {
            lua51::rawGetIndex(state, lua51::registryIndex, reference);
        }
        lua51::setGlobal(state, name);
        lua51::unreference(state, reference);
    }

    void clearPage() {
        lua51::lua_State* const state = lua51::getState();
        if (p_page.settings >= 0 && state != nullptr && state == p_page.state && lua51::ready()) {
            lua51::unreference(state, p_page.settings);
        }
        p_page.state = nullptr;
        p_page.settings = -1;
        p_page.error.clear();
    }

    bool callUpdate(lua51::lua_State* state) {
        lua51::getGlobal(state, "ModSettingsUpdate");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::pop(state, 1);
            return true;
        }

        lua51::getGlobal(state, "MOD_SETTING_SCOPE_RUNTIME");
        if (lua51::type(state, -1) != lua51::typeNumber) {
            lua51::pop(state, 1);
            lua51::pushNumber(state, 0.0);
        }
        const int result = lua51::pcall(state, 1, 0, 0);
        if (result != 0) {
            p_page.error = luaError(state);
            lua51::pop(state, 1);
            return false;
        }
        return true;
    }

    bool loadPage() {
        clearPage();
        lua51::lua_State* const state = lua51::getState();
        if (state == nullptr || !lua51::ready()) {
            p_page.error = "No active Noita Lua state is available yet.";
            return false;
        }

        const char* const names[] = {
            "mod_id",
            "mod_settings",
            "mod_settings_version",
            "ModSettingsUpdate",
            "ModSettingsGuiCount",
            "ModSettingsGui"
        };
        int saved[std::size(names)]{};
        for (std::size_t index = 0; index < std::size(names); ++index) {
            saved[index] = saveGlobal(state, names[index]);
            lua51::pushNil(state);
            lua51::setGlobal(state, names[index]);
        }

        const std::string path = "mods/" + p_page.id + "/settings.lua";
        lua51::getGlobal(state, "dofile");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::pop(state, 1);
            p_page.error = "Noita's dofile function is unavailable.";
        } else {
            lua51::pushString(state, path.c_str());
            p_loadingSettings = true;
            const int result = lua51::pcall(state, 1, 0, 0);
            p_loadingSettings = false;
            if (result != 0) {
                p_page.error = luaError(state);
                lua51::pop(state, 1);
            } else {
                callUpdate(state);
                lua51::getGlobal(state, "mod_settings");
                if (lua51::type(state, -1) == lua51::typeTable) {
                    p_page.settings = lua51::reference(state);
                    p_page.state = state;
                } else {
                    lua51::pop(state, 1);
                    if (p_page.error.empty()) {
                        p_page.error = "settings.lua did not create a mod_settings table.";
                    }
                }
            }
        }

        for (std::size_t index = 0; index < std::size(names); ++index) {
            restoreGlobal(state, names[index], saved[index]);
        }
        return p_page.settings >= 0;
    }

    std::string translated(lua51::lua_State* state, const std::string& text) {
        if (text.empty() || text.front() != '$') {
            return text;
        }

        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "GameTextGetTranslatedOrNot");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            return text;
        }
        lua51::pushString(state, text.c_str());
        if (lua51::pcall(state, 1, 1, 0) != 0) {
            lua51::setTop(state, top);
            return text;
        }
        const std::string result = stackText(state, -1);
        lua51::setTop(state, top);
        if (result.empty()) {
            return text;
        }
        return result;
    }

    Value getSetting(lua51::lua_State* state, const std::string& id, const Value& fallback) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ModSettingGetNextValue");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            return fallback;
        }
        lua51::pushString(state, id.c_str());
        if (lua51::pcall(state, 1, 1, 0) != 0) {
            lua51::setTop(state, top);
            return fallback;
        }
        Value value = stackValue(state, -1);
        lua51::setTop(state, top);
        if (value.type == ValueType::Nil) {
            value = fallback;
        }
        return value;
    }

    void setSetting(lua51::lua_State* state, const std::string& id, const Value& value) {
        const int top = lua51::getTop(state);
        lua51::getGlobal(state, "ModSettingSetNextValue");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            return;
        }
        lua51::pushString(state, id.c_str());
        pushValue(state, value);
        lua51::pushBoolean(state, false);
        if (lua51::pcall(state, 3, 0, 0) != 0) {
            sampo::log::error("Could not save secret mod setting: /arrow ID: %s /arrow Error: %s", id.c_str(), luaError(state).c_str());
        }
        lua51::setTop(state, top);
    }

    void callChange(lua51::lua_State* state, int setting, const Value& oldValue, const Value& newValue) {
        const int top = lua51::getTop(state);
        const int table = absoluteIndex(state, setting);
        lua51::getField(state, table, "change_fn");
        if (lua51::type(state, -1) != lua51::typeFunction) {
            lua51::setTop(state, top);
            return;
        }
        lua51::pushString(state, p_page.id.c_str());
        lua51::pushNil(state);
        lua51::pushBoolean(state, true);
        lua51::pushValue(state, table);
        pushValue(state, oldValue);
        pushValue(state, newValue);
        if (lua51::pcall(state, 6, 0, 0) != 0) {
            sampo::log::error("Secret mod setting callback failed: /arrow Mod: %s /arrow Error: %s", p_page.id.c_str(), luaError(state).c_str());
        }
        lua51::setTop(state, top);
    }

    void settingTooltip(const std::string& description) {
        if (description.empty() || !ImGui::IsItemHovered()) {
            return;
        }
        ImGui::SetTooltip("%s", description.c_str());
    }

    void drawSetting(lua51::lua_State* state, int setting) {
        const int table = absoluteIndex(state, setting);
        if (fieldBoolean(state, table, "hidden", false)) {
            return;
        }

        lua51::getField(state, table, "settings");
        const bool category = lua51::type(state, -1) == lua51::typeTable;
        const int childSettings = absoluteIndex(state, -1);
        if (category) {
            std::string name = fieldText(state, table, "ui_name");
            if (name.empty()) {
                name = fieldText(state, table, "category_id");
            }
            name = translated(state, name);
            if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                int item = 1;
                while (true) {
                    lua51::rawGetIndex(state, childSettings, item);
                    if (lua51::type(state, -1) == lua51::typeNil) {
                        lua51::pop(state, 1);
                        break;
                    }
                    if (lua51::type(state, -1) == lua51::typeTable) {
                        drawSetting(state, -1);
                    }
                    lua51::pop(state, 1);
                    ++item;
                }
                ImGui::Unindent();
            }
            lua51::pop(state, 1);
            return;
        }
        lua51::pop(state, 1);

        const std::string id = fieldText(state, table, "id");
        if (id.empty()) {
            return;
        }
        std::string name = fieldText(state, table, "ui_name");
        if (name.empty()) {
            name = id;
        }
        name = translated(state, name);
        const std::string description = translated(state, fieldText(state, table, "ui_description"));
        const Value fallback = fieldValue(state, table, "value_default");
        if (fallback.type == ValueType::Nil) {
            return;
        }

        const std::string fullId = p_page.id + "." + id;
        const Value oldValue = getSetting(state, fullId, fallback);
        Value newValue = oldValue;
        bool changed = false;
        ImGui::PushID(fullId.c_str());
        if (oldValue.type == ValueType::Boolean) {
            changed = ImGui::Checkbox(name.c_str(), &newValue.boolean);
            settingTooltip(description);
        } else if (oldValue.type == ValueType::Number) {
            double minimum = 0.0;
            double maximum = 0.0;
            const bool hasMinimum = fieldNumber(state, table, "value_min", minimum);
            const bool hasMaximum = fieldNumber(state, table, "value_max", maximum);
            if (hasMinimum && hasMaximum) {
                changed = ImGui::SliderScalar(name.c_str(), ImGuiDataType_Double, &newValue.number, &minimum, &maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            } else {
                changed = ImGui::InputDouble(name.c_str(), &newValue.number);
            }
            settingTooltip(description);
        } else if (oldValue.type == ValueType::String) {
            double maximumLength = 0.0;
            fieldNumber(state, table, "text_max_length", maximumLength);
            std::size_t capacity = 256;
            if (maximumLength > 0.0) {
                capacity = static_cast<std::size_t>(maximumLength) + 1;
            }
            if (capacity < oldValue.text.size() + 1) {
                capacity = oldValue.text.size() + 1;
            }
            std::vector<char> buffer(capacity, '\0');
            std::memcpy(buffer.data(), oldValue.text.data(), oldValue.text.size());
            if (ImGui::InputText(name.c_str(), buffer.data(), buffer.size())) {
                newValue.text = buffer.data();
                changed = true;
            }
            settingTooltip(description);
        }
        ImGui::PopID();

        if (changed) {
            setSetting(state, fullId, newValue);
            callChange(state, table, oldValue, newValue);
        }
    }

    void drawSettings(lua51::lua_State* state) {
        const int top = lua51::getTop(state);
        lua51::rawGetIndex(state, lua51::registryIndex, p_page.settings);
        if (lua51::type(state, -1) != lua51::typeTable) {
            lua51::setTop(state, top);
            ImGui::TextColored(settings::guiDangerColor(), "The mod settings table is no longer available.");
            return;
        }

        const int table = absoluteIndex(state, -1);
        int item = 1;
        while (true) {
            lua51::rawGetIndex(state, table, item);
            if (lua51::type(state, -1) == lua51::typeNil) {
                lua51::pop(state, 1);
                break;
            }
            if (lua51::type(state, -1) == lua51::typeTable) {
                drawSetting(state, -1);
            }
            lua51::pop(state, 1);
            ++item;
        }
        lua51::setTop(state, top);
    }
}

void mod_settings::setMods(const std::vector<Mod>& mods) {
    std::vector<std::string> paths;
    for (const Mod& mod : mods) {
        paths.push_back(normalize("mods/" + mod.id + "/settings.lua"));
        const std::filesystem::path directory(mod.directory);
        paths.push_back(normalize((directory / "settings.lua").string()));
        const std::string folder = directory.filename().string();
        if (!folder.empty()) {
            paths.push_back(normalize("mods/" + folder + "/settings.lua"));
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    std::scoped_lock lock(p_mutex);
    p_secretPaths = std::move(paths);
}

bool mod_settings::filterSource(const char* name, const char* source, std::size_t size, std::string& filteredSource) {
    if (p_loadingSettings || name == nullptr || source == nullptr || size == 0) {
        return false;
    }

    const std::string path = normalize(name);
    bool secret = false;
    {
        std::scoped_lock lock(p_mutex);
        for (const std::string& candidate : p_secretPaths) {
            if (path == candidate || endsWith(path, "/" + candidate) || endsWith(path, candidate)) {
                secret = true;
                break;
            }
        }
    }
    if (!secret) {
        return false;
    }

    filteredSource.assign(source, size);
    filteredSource += "\nfunction ModSettingsGuiCount() return 0 end\nfunction ModSettingsGui(gui, in_main_menu) end\n";
    return true;
}

void mod_settings::open(const std::string& id, const std::string& name) {
    p_page.id = id;
    p_page.name = name;
    p_page.requested = true;
}

void mod_settings::draw() {
    if (p_page.requested) {
        p_page.requested = false;
        p_page.visible = true;
        loadPage();
        ImGui::OpenPopup("Mod settings##SampoSecretSettings");
    }
    if (!p_page.visible) {
        return;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(display.x * 0.62f, display.y * 0.72f), ImGuiCond_Appearing);
    bool visible = p_page.visible;
    if (ImGui::BeginPopupModal("Mod settings##SampoSecretSettings", &visible, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%s - Mod settings", p_page.name.c_str());
        ImGui::Separator();
        if (!p_page.error.empty()) {
            ImGui::TextWrapped("%s", p_page.error.c_str());
            if (ImGui::Button("Retry")) {
                loadPage();
            }
        } else {
            lua51::lua_State* const state = lua51::getState();
            if (state == nullptr || state != p_page.state) {
                p_page.error = "Noita changed Lua states. Reopen or retry these settings.";
            } else {
                ImGui::BeginChild("##SecretSettings", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
                drawSettings(state);
                ImGui::EndChild();
            }
        }
        if (ImGui::Button("Close")) {
            visible = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!visible) {
        p_page.visible = false;
        clearPage();
    }
}
