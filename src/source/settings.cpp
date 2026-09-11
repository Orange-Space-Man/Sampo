#include "settings.h"

#include "log.h"
#include "noita_mainmenu.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

namespace {
    struct Values {
        bool noitaModCheck = false;
        bool useDefaultBuildText = false;
        bool useDefaultModsScreen = false;
        bool spellDescriptions = false;
        bool wandComparison = false;
        ImVec4 guiText{0.88f, 0.86f, 0.79f, 1.00f};
        ImVec4 guiMuted{0.46f, 0.45f, 0.41f, 1.00f};
        ImVec4 guiBorder{0.72f, 0.66f, 0.52f, 1.00f};
        ImVec4 guiAccent{0.96f, 0.62f, 0.18f, 1.00f};
        ImVec4 guiWindow{0.025f, 0.024f, 0.022f, 0.93f};
        ImVec4 guiPanel{0.035f, 0.033f, 0.030f, 0.86f};
        ImVec4 guiControl{0.30f, 0.19f, 0.09f, 0.96f};
        ImVec4 guiControlHover{0.43f, 0.28f, 0.12f, 0.98f};
        ImVec4 guiDanger{0.84f, 0.25f, 0.16f, 1.00f};
        ImVec4 logText{0.92f, 0.92f, 0.92f, 1.00f};
        ImVec4 logValue{0.35f, 0.62f, 0.95f, 1.00f};
        ImVec4 logArrow{0.46f, 0.45f, 0.41f, 1.00f};
        ImVec4 logError{0.95f, 0.24f, 0.22f, 1.00f};
        ImVec4 logErrorArrow{0.50f, 0.16f, 0.14f, 1.00f};
    };

    struct ColorValue {
        const char* name;
        ImVec4* value;
    };

    Values p_values;
    const std::array<ColorValue, 14> p_colors{{
        {"gui_text", &p_values.guiText},
        {"gui_muted", &p_values.guiMuted},
        {"gui_border", &p_values.guiBorder},
        {"gui_accent", &p_values.guiAccent},
        {"gui_window", &p_values.guiWindow},
        {"gui_panel", &p_values.guiPanel},
        {"gui_control", &p_values.guiControl},
        {"gui_control_hover", &p_values.guiControlHover},
        {"gui_danger", &p_values.guiDanger},
        {"log_text", &p_values.logText},
        {"log_value", &p_values.logValue},
        {"log_arrow", &p_values.logArrow},
        {"log_error", &p_values.logError},
        {"log_error_arrow", &p_values.logErrorArrow}
    }};

    std::filesystem::path noitaPath() {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        return std::filesystem::path(executable).parent_path();
    }

    std::filesystem::path settingsPath() {
        return noitaPath() / "sampo" / "sampo_settings.ini";
    }

    std::filesystem::path oldSettingsPath() {
        return noitaPath() / "sampo_settings.ini";
    }

    std::string trim(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    bool readColor(const std::string& text, ImVec4& color) {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        if (sscanf_s(text.c_str(), "%f,%f,%f,%f", &red, &green, &blue, &alpha) != 4) {
            return false;
        }

        color.x = std::clamp(red, 0.0f, 1.0f);
        color.y = std::clamp(green, 0.0f, 1.0f);
        color.z = std::clamp(blue, 0.0f, 1.0f);
        color.w = std::clamp(alpha, 0.0f, 1.0f);
        return true;
    }

    bool load(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file) {
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            const std::size_t split = line.find('=');
            if (split == std::string::npos) {
                continue;
            }

            const std::string name = trim(line.substr(0, split));
            const std::string value = trim(line.substr(split + 1));
            if (name == "noita_mod_check") {
                p_values.noitaModCheck = value == "1" || value == "true";
                continue;
            }
            if (name == "use_default_build_text") {
                p_values.useDefaultBuildText = value == "1" || value == "true";
                continue;
            }
            if (name == "use_default_mods_screen") {
                p_values.useDefaultModsScreen = value == "1" || value == "true";
                continue;
            }
            if (name == "spell_descriptions") {
                p_values.spellDescriptions = value == "1" || value == "true";
                continue;
            }
            if (name == "wand_comparison") {
                p_values.wandComparison = value == "1" || value == "true";
                continue;
            }

            for (const ColorValue& color : p_colors) {
                if (name == color.name) {
                    readColor(value, *color.value);
                    break;
                }
            }
        }
        return true;
    }

    bool save() {
        const std::filesystem::path path = settingsPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::trunc);
        if (!file) {
            sampo::log::error("Could not save Sampo settings");
            return false;
        }

        file << "noita_mod_check=";
        if (p_values.noitaModCheck) {
            file << "1\n";
        } else {
            file << "0\n";
        }
        file << "use_default_build_text=";
        if (p_values.useDefaultBuildText) {
            file << "1\n";
        } else {
            file << "0\n";
        }
        file << "use_default_mods_screen=";
        if (p_values.useDefaultModsScreen) {
            file << "1\n";
        } else {
            file << "0\n";
        }
        file << "spell_descriptions=";
        if (p_values.spellDescriptions) {
            file << "1\n";
        } else {
            file << "0\n";
        }
        file << "wand_comparison=";
        if (p_values.wandComparison) {
            file << "1\n";
        } else {
            file << "0\n";
        }
        file << std::fixed << std::setprecision(3);
        for (const ColorValue& color : p_colors) {
            file << color.name << '=' << color.value->x << ',' << color.value->y << ',' << color.value->z << ',' << color.value->w << '\n';
        }
        return true;
    }

    bool drawColor(const char* label, ImVec4& color) {
        ImGui::PushID(&color);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(170.0f);
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
        constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
        const bool changed = ImGui::ColorEdit4("##Color", &color.x, flags);
        ImGui::PopID();
        return changed;
    }
}

bool settings::init() {
    const std::filesystem::path path = settingsPath();
    if (load(path)) {
        return true;
    }

    const std::filesystem::path oldPath = oldSettingsPath();
    if (load(oldPath)) {
        if (save()) {
            std::error_code error;
            std::filesystem::remove(oldPath, error);
        }
        return true;
    }
    save();
    return true;
}

bool settings::noitaModCheck() {
    return p_values.noitaModCheck;
}

bool settings::useDefaultBuildText() {
    return p_values.useDefaultBuildText;
}

bool settings::useDefaultModsScreen() {
    return p_values.useDefaultModsScreen;
}

bool settings::spellDescriptions() {
    return p_values.spellDescriptions;
}

bool settings::wandComparison() {
    return p_values.wandComparison;
}

void settings::applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowPadding = ImVec2(8.0f, 7.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.CellPadding = ImVec2(10.0f, 6.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 9.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = p_values.guiText;
    colors[ImGuiCol_TextDisabled] = p_values.guiMuted;
    colors[ImGuiCol_WindowBg] = p_values.guiWindow;
    colors[ImGuiCol_ChildBg] = p_values.guiPanel;
    colors[ImGuiCol_PopupBg] = p_values.guiWindow;
    colors[ImGuiCol_Border] = p_values.guiBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = p_values.guiControl;
    colors[ImGuiCol_FrameBgHovered] = p_values.guiControlHover;
    colors[ImGuiCol_FrameBgActive] = p_values.guiControlHover;
    colors[ImGuiCol_TitleBg] = p_values.guiWindow;
    colors[ImGuiCol_TitleBgActive] = p_values.guiControl;
    colors[ImGuiCol_MenuBarBg] = p_values.guiWindow;
    colors[ImGuiCol_ScrollbarBg] = p_values.guiWindow;
    colors[ImGuiCol_ScrollbarGrab] = p_values.guiMuted;
    colors[ImGuiCol_ScrollbarGrabHovered] = p_values.guiBorder;
    colors[ImGuiCol_ScrollbarGrabActive] = p_values.guiAccent;
    colors[ImGuiCol_CheckMark] = p_values.guiAccent;
    colors[ImGuiCol_SliderGrab] = p_values.guiBorder;
    colors[ImGuiCol_SliderGrabActive] = p_values.guiAccent;
    colors[ImGuiCol_Button] = p_values.guiControl;
    colors[ImGuiCol_ButtonHovered] = p_values.guiControlHover;
    colors[ImGuiCol_ButtonActive] = p_values.guiDanger;
    colors[ImGuiCol_Header] = p_values.guiControl;
    colors[ImGuiCol_HeaderHovered] = p_values.guiControlHover;
    colors[ImGuiCol_HeaderActive] = p_values.guiDanger;
    colors[ImGuiCol_Separator] = p_values.guiMuted;
    colors[ImGuiCol_SeparatorHovered] = p_values.guiBorder;
    colors[ImGuiCol_SeparatorActive] = p_values.guiAccent;
    colors[ImGuiCol_Tab] = p_values.guiPanel;
    colors[ImGuiCol_TabHovered] = p_values.guiControlHover;
    colors[ImGuiCol_TabSelected] = p_values.guiControl;
    colors[ImGuiCol_TabSelectedOverline] = p_values.guiBorder;
    colors[ImGuiCol_TabDimmedSelected] = p_values.guiPanel;
    colors[ImGuiCol_TabDimmedSelectedOverline] = p_values.guiMuted;
    colors[ImGuiCol_TableHeaderBg] = p_values.guiControl;
    colors[ImGuiCol_TableBorderStrong] = p_values.guiBorder;
    colors[ImGuiCol_TableBorderLight] = p_values.guiMuted;
    colors[ImGuiCol_TableRowBgAlt] = p_values.guiControl;
    colors[ImGuiCol_NavCursor] = p_values.guiAccent;
    colors[ImGuiCol_TextLink] = p_values.guiAccent;
    colors[ImGuiCol_PlotHistogram] = p_values.guiAccent;
}

void settings::draw(float top) {
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, displaySize.y - top), ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    bool colorsChanged = false;
    bool modCheckChanged = false;
    bool buildTextChanged = false;
    bool modsScreenChanged = false;
    bool spellDescriptionsChanged = false;
    bool wandComparisonChanged = false;

    if (ImGui::Begin("##SampoSettings", nullptr, flags)) {
        ImGui::TextUnformatted("Sampo");
        ImGui::Separator();
        buildTextChanged = ImGui::Checkbox("Use default build text", &p_values.useDefaultBuildText);
        ImGui::TextDisabled("Shows Noita's original build text without the Sampo build line.");
        modsScreenChanged = ImGui::Checkbox("Use default Mods screen", &p_values.useDefaultModsScreen);
        ImGui::TextDisabled("The main-menu Mods button opens Noita's standard Mods screen.");
        ImGui::Spacing();

        ImGui::TextUnformatted("Noita");
        ImGui::Separator();
        modCheckChanged = ImGui::Checkbox("Let Noita detect active mods", &p_values.noitaModCheck);
        ImGui::TextDisabled("When disabled, Noita will not mark the current run as modded.");
        ImGui::Spacing();

        ImGui::TextUnformatted("Built-in Mods");
        ImGui::Separator();
        spellDescriptionsChanged = ImGui::Checkbox("Spell descriptions", &p_values.spellDescriptions);
        ImGui::TextDisabled("Shows each spell's description in its world pickup hint.");
        wandComparisonChanged = ImGui::Checkbox("Wand comparisons", &p_values.wandComparison);
        ImGui::TextDisabled("Colors dropped-wand stats against the wand currently held.");
        ImGui::Spacing();

        if (ImGui::BeginTable("##SettingColors", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Interface colors");
            ImGui::Separator();
            colorsChanged |= drawColor("Text", p_values.guiText);
            colorsChanged |= drawColor("Muted text", p_values.guiMuted);
            colorsChanged |= drawColor("Border", p_values.guiBorder);
            colorsChanged |= drawColor("Accent", p_values.guiAccent);
            colorsChanged |= drawColor("Window", p_values.guiWindow);
            colorsChanged |= drawColor("Panel", p_values.guiPanel);
            colorsChanged |= drawColor("Control", p_values.guiControl);
            colorsChanged |= drawColor("Control hover", p_values.guiControlHover);
            colorsChanged |= drawColor("Danger", p_values.guiDanger);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Log colors");
            ImGui::Separator();
            colorsChanged |= drawColor("Text", p_values.logText);
            colorsChanged |= drawColor("Values", p_values.logValue);
            colorsChanged |= drawColor("Arrows", p_values.logArrow);
            colorsChanged |= drawColor("Errors", p_values.logError);
            colorsChanged |= drawColor("Error arrows", p_values.logErrorArrow);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset colors")) {
            const bool noitaModCheck = p_values.noitaModCheck;
            const bool useDefaultBuildText = p_values.useDefaultBuildText;
            const bool useDefaultModsScreen = p_values.useDefaultModsScreen;
            const bool spellDescriptions = p_values.spellDescriptions;
            const bool wandComparison = p_values.wandComparison;
            p_values = Values{};
            p_values.noitaModCheck = noitaModCheck;
            p_values.useDefaultBuildText = useDefaultBuildText;
            p_values.useDefaultModsScreen = useDefaultModsScreen;
            p_values.spellDescriptions = spellDescriptions;
            p_values.wandComparison = wandComparison;
            colorsChanged = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Changes are saved automatically.");
    }
    ImGui::End();

    bool settingsChanged = false;
    if (modCheckChanged) {
        if (noita_mainmenu::setNoitaModCheck(p_values.noitaModCheck)) {
            settingsChanged = true;
        } else {
            p_values.noitaModCheck = !p_values.noitaModCheck;
        }
    }
    if (buildTextChanged) {
        if (noita_mainmenu::setDefaultBuildText(p_values.useDefaultBuildText)) {
            settingsChanged = true;
        } else {
            p_values.useDefaultBuildText = !p_values.useDefaultBuildText;
        }
    }
    if (modsScreenChanged) {
        if (noita_mainmenu::setDefaultModsScreen(p_values.useDefaultModsScreen)) {
            settingsChanged = true;
        } else {
            p_values.useDefaultModsScreen = !p_values.useDefaultModsScreen;
        }
    }
    if (colorsChanged) {
        applyTheme();
    }
    if (settingsChanged || colorsChanged || spellDescriptionsChanged || wandComparisonChanged) {
        save();
    }
}

const ImVec4& settings::guiDangerColor() {
    return p_values.guiDanger;
}

const ImVec4& settings::logTextColor() {
    return p_values.logText;
}

const ImVec4& settings::logValueColor() {
    return p_values.logValue;
}

const ImVec4& settings::logArrowColor() {
    return p_values.logArrow;
}

const ImVec4& settings::logErrorColor() {
    return p_values.logError;
}

const ImVec4& settings::logErrorArrowColor() {
    return p_values.logErrorArrow;
}
