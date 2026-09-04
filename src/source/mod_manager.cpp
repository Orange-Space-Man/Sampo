#include "mod_manager.h"

#include "log.h"
#include "noita.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    struct Mod {
        std::string key;
        std::string id;
        std::string name;
        std::string description;
        std::string author;
        std::string version;
        std::string builtWith;
        std::string directory;
        std::string workshopId;
        std::string sampoVersion;
        std::vector<std::string> files;
        std::uintmax_t size = 0;
        bool sampo = false;
        bool workshop = false;
        bool enabled = false;
        bool hasInit = false;
        bool hasSettingsLua = false;
        bool hasSettingsXml = false;
        bool unrestricted = false;
    };

    struct Snapshot {
        std::vector<Mod> mods;
        std::vector<Mod> active;
        std::string error;
    };

    std::mutex p_mutex;
    std::vector<Mod> p_mods;
    std::unordered_map<std::string, std::string> p_configBlocks;
    std::vector<std::string> p_configOrder;
    std::filesystem::path p_modsDirectory;
    std::filesystem::path p_workshopDirectory;
    std::filesystem::path p_configPath;
    std::string p_error;
    std::string p_draggedName;
    std::string p_selectedKey;
    ImVec2 p_draggedSize;
    ImVec2 p_draggedOffset;
    bool p_draggedSampo = false;

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::string trim(std::string text) {
        std::size_t first = 0;
        while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
            ++first;
        }

        std::size_t last = text.size();
        while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
            --last;
        }
        return text.substr(first, last - first);
    }

    std::string lower(std::string text) {
        for (char& character : text) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return text;
    }

    std::string attribute(const std::string& xml, const char* name) {
        const std::regex expression(std::string("\\b") + name + R"(\s*=\s*(["'])(.*?)\1)", std::regex::icase);
        std::smatch match;
        if (!std::regex_search(xml, match, expression)) {
            return {};
        }
        return match[2].str();
    }

    std::string decodeXml(std::string text) {
        const std::pair<const char*, const char*> replacements[] = {
            {"&quot;", "\""},
            {"&apos;", "'"},
            {"&lt;", "<"},
            {"&gt;", ">"},
            {"&amp;", "&"},
            {"\\n", "\n"}
        };
        for (const auto& replacement : replacements) {
            std::size_t position = 0;
            while (true) {
                position = text.find(replacement.first, position);
                if (position == std::string::npos) {
                    break;
                }
                text.replace(position, std::strlen(replacement.first), replacement.second);
                position += std::strlen(replacement.second);
            }
        }
        return text;
    }

    std::string encodeXml(std::string text) {
        const std::pair<const char*, const char*> replacements[] = {
            {"&", "&amp;"},
            {"\"", "&quot;"},
            {"'", "&apos;"},
            {"<", "&lt;"},
            {">", "&gt;"}
        };
        for (const auto& replacement : replacements) {
            std::size_t position = 0;
            while (true) {
                position = text.find(replacement.first, position);
                if (position == std::string::npos) {
                    break;
                }
                text.replace(position, std::strlen(replacement.first), replacement.second);
                position += std::strlen(replacement.second);
            }
        }
        return text;
    }

    std::filesystem::path configPath() {
        const DWORD required = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
        if (required <= 1) {
            return {};
        }

        std::wstring profile(required, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile.data(), required);
        if (length == 0 || length >= required) {
            return {};
        }
        profile.resize(length);
        return std::filesystem::path(profile) / "AppData" / "LocalLow" / "Nolla_Games_Noita" / "save00" / "mod_config.xml";
    }

    void scanFiles(Mod& mod, const std::filesystem::path& directory) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(directory, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            std::error_code itemError;
            if (iterator->is_regular_file(itemError) && !itemError) {
                const std::uintmax_t fileSize = iterator->file_size(itemError);
                if (!itemError) {
                    mod.size += fileSize;
                    mod.files.push_back(iterator->path().lexically_relative(directory).generic_string());
                }
            }
            iterator.increment(error);
        }
        std::sort(mod.files.begin(), mod.files.end());
    }

    std::string sizeText(std::uintmax_t size) {
        constexpr const char* units[] = {"B", "KB", "MB", "GB"};
        double value = static_cast<double>(size);
        std::size_t unit = 0;
        while (value >= 1024.0 && unit + 1 < std::size(units)) {
            value /= 1024.0;
            ++unit;
        }

        char text[32]{};
        if (unit == 0) {
            std::snprintf(text, std::size(text), "%llu %s", static_cast<unsigned long long>(size), units[unit]);
        } else {
            std::snprintf(text, std::size(text), "%.1f %s", value, units[unit]);
        }
        return text;
    }

    std::string configKey(const std::string& id, const std::string& workshopId) {
        if (!workshopId.empty() && workshopId != "0") {
            return "workshop:" + workshopId;
        }
        return id;
    }

    void readConfig(std::unordered_map<std::string, bool>& enabled) {
        p_configBlocks.clear();
        p_configOrder.clear();
        const std::string xml = readFile(p_configPath);
        const std::regex expression(R"(<Mod\b[^>]*>[\s\S]*?</Mod>)", std::regex::icase);
        for (std::sregex_iterator item(xml.begin(), xml.end(), expression), end; item != end; ++item) {
            const std::string block = item->str();
            const std::string id = decodeXml(attribute(block, "name"));
            const std::string workshopId = attribute(block, "workshop_item_id");
            const std::string key = configKey(id, workshopId);
            if (key.empty()) {
                continue;
            }

            enabled[key] = attribute(block, "enabled") == "1";
            p_configBlocks[key] = block;
            p_configOrder.push_back(key);
        }
    }

    void addMod(std::unordered_map<std::string, Mod>& found, const std::filesystem::path& directory, const std::string& key, const std::string& id, const std::string& workshopId, const std::unordered_map<std::string, bool>& enabled) {
        if (id.empty() || id[0] == '.') {
            return;
        }

        Mod mod;
        mod.key = key;
        mod.id = id;
        mod.directory = directory.string();
        mod.workshopId = workshopId;
        mod.workshop = !workshopId.empty();
        const std::string manifest = readFile(directory / "mod.xml");
        const std::string compatibility = readFile(directory / "compatibility.xml");
        mod.name = decodeXml(attribute(manifest, "name"));
        if (mod.name.empty()) {
            mod.name = mod.id;
        }
        mod.description = decodeXml(attribute(manifest, "description"));
        mod.author = decodeXml(attribute(manifest, "author"));
        mod.version = decodeXml(attribute(manifest, "version"));
        mod.builtWith = decodeXml(attribute(manifest, "version_built_with"));
        if (mod.builtWith.empty()) {
            mod.builtWith = decodeXml(attribute(compatibility, "version_built_with"));
        }
        mod.sampoVersion = attribute(compatibility, "sampo");
        mod.sampo = (!mod.sampoVersion.empty() && mod.sampoVersion != "0") || attribute(compatibility, "nmdt") == "1";
        mod.unrestricted = attribute(manifest, "request_no_api_restrictions") == "1";
        const auto enabledEntry = enabled.find(mod.key);
        if (enabledEntry != enabled.end()) {
            mod.enabled = enabledEntry->second;
        }
        std::error_code fileError;
        mod.hasInit = std::filesystem::is_regular_file(directory / "init.lua", fileError) && !fileError;
        fileError.clear();
        mod.hasSettingsLua = std::filesystem::is_regular_file(directory / "settings.lua", fileError) && !fileError;
        fileError.clear();
        mod.hasSettingsXml = std::filesystem::is_regular_file(directory / "settings.xml", fileError) && !fileError;
        scanFiles(mod, directory);
        found.emplace(mod.key, std::move(mod));
    }

    void scanLocal(std::unordered_map<std::string, Mod>& found, const std::unordered_map<std::string, bool>& enabled) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(p_modsDirectory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            std::error_code itemError;
            if (iterator->is_directory(itemError) && !itemError) {
                const std::string id = iterator->path().filename().string();
                addMod(found, iterator->path(), id, id, {}, enabled);
            }
            iterator.increment(error);
        }
    }

    std::filesystem::path findWorkshopMod(const std::filesystem::path& directory) {
        std::error_code error;
        if (std::filesystem::is_regular_file(directory / "mod.xml", error) && !error) {
            return directory;
        }

        error.clear();
        std::filesystem::path result;
        std::size_t depth = (std::numeric_limits<std::size_t>::max)();
        std::filesystem::recursive_directory_iterator iterator(directory, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            std::error_code itemError;
            if (iterator->is_regular_file(itemError) && !itemError && iterator->path().filename() == "mod.xml") {
                const std::filesystem::path relative = iterator->path().parent_path().lexically_relative(directory);
                const std::size_t candidateDepth = static_cast<std::size_t>(std::distance(relative.begin(), relative.end()));
                if (candidateDepth < depth) {
                    depth = candidateDepth;
                    result = iterator->path().parent_path();
                }
            }
            iterator.increment(error);
        }
        return result;
    }

    void scanWorkshop(std::unordered_map<std::string, Mod>& found, const std::unordered_map<std::string, bool>& enabled) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(p_workshopDirectory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            std::error_code itemError;
            if (!iterator->is_directory(itemError) || itemError) {
                iterator.increment(error);
                continue;
            }

            const std::string workshopId = iterator->path().filename().string();
            const bool numeric = !workshopId.empty() && std::all_of(workshopId.begin(), workshopId.end(), [](unsigned char character) {
                return std::isdigit(character) != 0;
            });
            if (!numeric) {
                iterator.increment(error);
                continue;
            }

            const std::filesystem::path modDirectory = findWorkshopMod(iterator->path());
            if (!modDirectory.empty()) {
                const std::string manifest = readFile(modDirectory / "mod.xml");
                std::string id = trim(readFile(modDirectory / "mod_id.txt"));
                if (id.empty()) {
                    id = decodeXml(attribute(manifest, "name"));
                }
                if (id.empty()) {
                    id = modDirectory.filename().string();
                }
                addMod(found, modDirectory, "workshop:" + workshopId, id, workshopId, enabled);
            }
            iterator.increment(error);
        }
    }

    void refreshLocked() {
        std::unordered_map<std::string, bool> enabled;
        readConfig(enabled);

        std::error_code error;
        if (!std::filesystem::is_directory(p_modsDirectory, error)) {
            p_error = "Mods directory not found: " + p_modsDirectory.string();
            p_mods.clear();
            return;
        }

        std::unordered_map<std::string, Mod> found;
        scanLocal(found, enabled);
        scanWorkshop(found, enabled);
        p_mods.clear();
        for (const std::string& key : p_configOrder) {
            auto item = found.find(key);
            if (item == found.end()) {
                continue;
            }
            p_mods.push_back(std::move(item->second));
            found.erase(item);
        }

        std::vector<Mod> additions;
        additions.reserve(found.size());
        for (auto& item : found) {
            additions.push_back(std::move(item.second));
        }
        std::sort(additions.begin(), additions.end(), [](const Mod& left, const Mod& right) {
            return left.name < right.name;
        });
        p_mods.insert(p_mods.end(), std::make_move_iterator(additions.begin()), std::make_move_iterator(additions.end()));
        p_error.clear();
    }

    std::string setEnabledAttribute(std::string block, bool enabled) {
        const std::regex expression(R"(\benabled\s*=\s*["'][^"']*["'])", std::regex::icase);
        std::string value = "enabled=\"0\"";
        if (enabled) {
            value = "enabled=\"1\"";
        }
        if (std::regex_search(block, expression)) {
            return std::regex_replace(block, expression, value, std::regex_constants::format_first_only);
        }

        const std::size_t close = block.find('>');
        if (close != std::string::npos) {
            block.insert(close, " " + value);
        }
        return block;
    }

    bool saveLocked() {
        if (p_configPath.empty()) {
            p_error = "Noita mod configuration path is unavailable";
            return false;
        }

        std::string xml = "<Mods>\r\n";
        std::unordered_map<std::string, bool> written;
        const std::vector<std::string> previousOrder = p_configOrder;
        p_configOrder.clear();
        for (const Mod& mod : p_mods) {
            std::string block;
            const auto existing = p_configBlocks.find(mod.key);
            if (existing != p_configBlocks.end()) {
                block = setEnabledAttribute(existing->second, mod.enabled);
            } else {
                block = "  <Mod enabled=\"";
                if (mod.enabled) {
                    block += "1";
                } else {
                    block += "0";
                }
                block += "\" name=\"" + encodeXml(mod.id) + "\" settings_fold_open=\"0\" workshop_item_id=\"";
                if (mod.workshop) {
                    block += mod.workshopId;
                } else {
                    block += "0";
                }
                block += "\" >\r\n  </Mod>";
            }
            xml += block;
            xml += "\r\n";
            p_configBlocks[mod.key] = block;
            p_configOrder.push_back(mod.key);
            written[mod.key] = true;
        }

        for (const std::string& key : previousOrder) {
            if (written.contains(key)) {
                continue;
            }
            const auto item = p_configBlocks.find(key);
            if (item == p_configBlocks.end()) {
                continue;
            }
            xml += item->second;
            xml += "\r\n";
            p_configOrder.push_back(key);
            written[key] = true;
        }
        for (const auto& item : p_configBlocks) {
            if (written.contains(item.first)) {
                continue;
            }
            xml += item.second;
            xml += "\r\n";
            p_configOrder.push_back(item.first);
        }
        xml += "</Mods>\r\n";

        const std::filesystem::path temporary = p_configPath.string() + ".sampo.tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                p_error = "Could not write " + temporary.string();
                return false;
            }
            file.write(xml.data(), static_cast<std::streamsize>(xml.size()));
        }

        if (!MoveFileExW(temporary.c_str(), p_configPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD error = GetLastError();
            DeleteFileW(temporary.c_str());
            p_error = "Could not update mod_config.xml, error " + std::to_string(error);
            return false;
        }
        p_error.clear();
        return true;
    }

    bool refreshRuntime() {
        if (noita::noitaBase == nullptr) {
            return false;
        }

        std::uint8_t* const function = reinterpret_cast<std::uint8_t*>(noita::noitaBase) + noita::refreshModsRva;
        constexpr std::uint8_t expected[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68};
        if (std::memcmp(function, expected, sizeof(expected)) != 0) {
            sampo::log::error("Could not refresh Noita mods: /arrow Unsupported noita.exe build /arrow Address: %p", function);
            return false;
        }

        using RefreshMods = void(__cdecl*)();
        reinterpret_cast<RefreshMods>(function)();
        return true;
    }

    Snapshot snapshot() {
        std::scoped_lock lock(p_mutex);
        Snapshot result;
        result.mods = p_mods;
        result.error = p_error;
        for (const Mod& mod : p_mods) {
            if (mod.enabled) {
                result.active.push_back(mod);
            }
        }
        return result;
    }

    void setEnabled(const std::string& key, bool enabled) {
        bool saved = false;
        {
            std::scoped_lock lock(p_mutex);
            const auto mod = std::find_if(p_mods.begin(), p_mods.end(), [&](const Mod& item) {
                return item.key == key;
            });
            if (mod == p_mods.end() || mod->enabled == enabled) {
                return;
            }

            if (enabled) {
                for (Mod& item : p_mods) {
                    if (item.key != mod->key && item.id == mod->id) {
                        item.enabled = false;
                    }
                }
            }
            mod->enabled = enabled;
            saved = saveLocked();
        }
        if (saved) {
            refreshRuntime();
        }
    }

    void moveMod(const std::string& key, const std::string& targetKey, bool after) {
        if (key == targetKey) {
            return;
        }

        bool saved = false;
        {
            std::scoped_lock lock(p_mutex);
            auto source = std::find_if(p_mods.begin(), p_mods.end(), [&](const Mod& item) {
                return item.key == key;
            });
            auto target = std::find_if(p_mods.begin(), p_mods.end(), [&](const Mod& item) {
                return item.key == targetKey;
            });
            if (source == p_mods.end() || target == p_mods.end() || !source->enabled || !target->enabled) {
                return;
            }

            Mod moving = std::move(*source);
            p_mods.erase(source);
            target = std::find_if(p_mods.begin(), p_mods.end(), [&](const Mod& item) {
                return item.key == targetKey;
            });
            if (target == p_mods.end()) {
                return;
            }
            if (after) {
                ++target;
            }
            p_mods.insert(target, std::move(moving));
            saved = saveLocked();
        }
        if (saved) {
            refreshRuntime();
        }
    }

    void pushModColor(const Mod& mod) {
        if (mod.sampo) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.50f, 0.12f, 1.0f));
        }
    }

    void popModColor(const Mod& mod) {
        if (mod.sampo) {
            ImGui::PopStyleColor();
        }
    }

    void drawInactive(const std::vector<Mod>& mods) {
        std::size_t inactive = 0;
        for (const Mod& mod : mods) {
            if (!mod.enabled) {
                ++inactive;
            }
        }

        ImGui::Text("Inactive Mods (%zu)", inactive);
        ImGui::Separator();
        for (const Mod& mod : mods) {
            if (mod.enabled) {
                continue;
            }

            ImGui::PushID(mod.key.c_str());
            pushModColor(mod);
            if (ImGui::Selectable(mod.name.c_str())) {
                p_selectedKey = mod.key;
                setEnabled(mod.key, true);
            }
            popModColor(mod);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable %s", mod.name.c_str());
            }
            ImGui::PopID();
        }
    }

    void drawDragPreview() {
        const ImGuiPayload* const payload = ImGui::GetDragDropPayload();
        if (payload == nullptr || !payload->IsDataType("SAMPO_MOD_ORDER") || p_draggedName.empty()) {
            p_draggedName.clear();
            p_draggedSize = {};
            p_draggedOffset = {};
            p_draggedSampo = false;
            return;
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 minimum(mouse.x - p_draggedOffset.x, mouse.y - p_draggedOffset.y);
        const ImVec2 maximum(minimum.x + p_draggedSize.x, minimum.y + p_draggedSize.y);
        ImDrawList* const drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(minimum, maximum, IM_COL32(62, 56, 43, 245));
        unsigned int textColor = IM_COL32(240, 236, 220, 255);
        if (p_draggedSampo) {
            textColor = IM_COL32(255, 128, 31, 255);
        }
        const float textY = minimum.y + (p_draggedSize.y - ImGui::GetFontSize()) * 0.5f;
        drawList->AddText(ImVec2(minimum.x + ImGui::GetStyle().FramePadding.x, textY), textColor, p_draggedName.c_str());
    }

    void drawActive(const std::vector<Mod>& mods) {
        ImGui::Text("Active Mods (%zu)", mods.size());
        ImGui::Separator();
        for (std::size_t index = 0; index < mods.size(); ++index) {
            const Mod& mod = mods[index];
            ImGui::PushID(mod.key.c_str());
            pushModColor(mod);
            const bool selected = p_selectedKey == mod.key;
            if (ImGui::Selectable(mod.name.c_str(), selected, ImGuiSelectableFlags_AllowOverlap)) {
                p_selectedKey = mod.key;
            }
            popModColor(mod);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
                ImGui::SetDragDropPayload("SAMPO_MOD_ORDER", mod.key.c_str(), mod.key.size() + 1);
                if (p_draggedName.empty()) {
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const ImVec2 minimum = ImGui::GetItemRectMin();
                    const ImVec2 maximum = ImGui::GetItemRectMax();
                    p_draggedName = mod.name;
                    p_draggedSize = ImVec2(maximum.x - minimum.x, maximum.y - minimum.y);
                    p_draggedOffset = ImVec2(mouse.x - minimum.x, mouse.y - minimum.y);
                    p_draggedSampo = mod.sampo;
                }
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                constexpr ImGuiDragDropFlags dropFlags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload("SAMPO_MOD_ORDER", dropFlags);
                if (payload != nullptr && payload->Data != nullptr) {
                    const bool after = ImGui::GetMousePos().y > (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
                    float lineY = ImGui::GetItemRectMin().y;
                    if (after) {
                        lineY = ImGui::GetItemRectMax().y;
                    }
                    ImDrawList* const drawList = ImGui::GetWindowDrawList();
                    drawList->AddLine(ImVec2(ImGui::GetItemRectMin().x, lineY), ImVec2(ImGui::GetItemRectMax().x, lineY), IM_COL32(255, 151, 48, 255), 2.0f);
                    const char* const source = static_cast<const char*>(payload->Data);
                    if (payload->Delivery && mod.key != source) {
                        moveMod(source, mod.key, after);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }

        if (!mods.empty()) {
            float dropHeight = ImGui::GetContentRegionAvail().y;
            if (dropHeight < 8.0f) {
                dropHeight = 8.0f;
            }
            ImGui::InvisibleButton("##ActiveDropEnd", ImVec2(-1.0f, dropHeight));
            if (ImGui::BeginDragDropTarget()) {
                constexpr ImGuiDragDropFlags dropFlags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload("SAMPO_MOD_ORDER", dropFlags);
                if (payload != nullptr && payload->Data != nullptr) {
                    const float lineY = ImGui::GetItemRectMin().y;
                    ImDrawList* const drawList = ImGui::GetWindowDrawList();
                    drawList->AddLine(ImVec2(ImGui::GetItemRectMin().x, lineY), ImVec2(ImGui::GetItemRectMax().x, lineY), IM_COL32(255, 151, 48, 255), 2.0f);
                    if (payload->Delivery) {
                        const char* const source = static_cast<const char*>(payload->Data);
                        moveMod(source, mods.back().key, true);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        drawDragPreview();
    }

    void detailRow(const char* name, const std::string& value) {
        if (value.empty()) {
            return;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", name);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", value.c_str());
    }

    void drawFileRange(const std::vector<std::string>& files, std::size_t first, std::size_t last, const std::string& prefix) {
        std::size_t index = first;
        while (index < last) {
            const std::string remaining = files[index].substr(prefix.size());
            const std::size_t slash = remaining.find('/');
            if (slash == std::string::npos) {
                ImGui::BulletText("%s", remaining.c_str());
                ++index;
                continue;
            }

            const std::string directory = remaining.substr(0, slash);
            const std::string directoryPrefix = prefix + directory + "/";
            std::size_t directoryEnd = index + 1;
            while (directoryEnd < last && files[directoryEnd].compare(0, directoryPrefix.size(), directoryPrefix) == 0) {
                ++directoryEnd;
            }

            if (ImGui::TreeNodeEx(directoryPrefix.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s", directory.c_str())) {
                drawFileRange(files, index, directoryEnd, directoryPrefix);
                ImGui::TreePop();
            }
            index = directoryEnd;
        }
    }

    void drawDetails(const std::vector<Mod>& mods) {
        const Mod* selected = nullptr;
        std::size_t loadOrder = 0;
        for (std::size_t index = 0; index < mods.size(); ++index) {
            if (mods[index].key == p_selectedKey) {
                selected = &mods[index];
                loadOrder = index + 1;
                break;
            }
        }
        if (selected == nullptr && !mods.empty()) {
            selected = &mods.front();
            p_selectedKey = selected->key;
            loadOrder = 1;
        }

        ImGui::TextUnformatted("Mod Details");
        ImGui::Separator();
        if (selected == nullptr) {
            ImGui::TextDisabled("Select an active mod to view its details.");
            return;
        }

        pushModColor(*selected);
        ImGui::TextWrapped("%s", selected->name.c_str());
        popModColor(*selected);
        if (!selected->description.empty()) {
            pushModColor(*selected);
            ImGui::TextWrapped("%s", selected->description.c_str());
            popModColor(*selected);
        }
        ImGui::Spacing();

        std::string type = "Noita mod";
        if (selected->sampo) {
            type = "Sampo mod";
        }
        std::string source = "Local";
        if (selected->workshop) {
            source = "Steam Workshop";
        }
        std::string hasInit = "No";
        if (selected->hasInit) {
            hasInit = "Yes";
        }
        std::string hasSettings = "No";
        if (selected->hasSettingsLua || selected->hasSettingsXml) {
            hasSettings = "Yes";
        }
        std::string unrestricted = "Not requested";
        if (selected->unrestricted) {
            unrestricted = "Requested";
        }

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##ModDetails", 2, tableFlags)) {
            const float detailNameWidth = ImGui::CalcTextSize("Unrestricted API").x + ImGui::GetStyle().CellPadding.x * 2.0f;
            ImGui::TableSetupColumn("##DetailName", ImGuiTableColumnFlags_WidthFixed, detailNameWidth);
            ImGui::TableSetupColumn("##DetailValue", ImGuiTableColumnFlags_WidthStretch);
            detailRow("ID", selected->id);
            detailRow("Author", selected->author);
            detailRow("Version", selected->version);
            detailRow("Built with", selected->builtWith);
            detailRow("Type", type);
            detailRow("Sampo version", selected->sampoVersion);
            detailRow("Source", source);
            detailRow("Workshop ID", selected->workshopId);
            detailRow("Load order", std::to_string(loadOrder));
            detailRow("Size", sizeText(selected->size));
            detailRow("Files", std::to_string(selected->files.size()));
            detailRow("init.lua", hasInit);
            detailRow("Settings", hasSettings);
            detailRow("Unrestricted API", unrestricted);
            detailRow("Installed path", selected->directory);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Disable Mod")) {
            const std::string key = selected->key;
            p_selectedKey.clear();
            setEnabled(key, false);
            return;
        }

        ImGui::Spacing();
        ImGui::Text("Files (%zu)", selected->files.size());
        ImGui::Separator();
        ImGui::BeginChild("##ModFiles", ImVec2(0.0f, -1.0f), false);
        if (selected->files.empty()) {
            ImGui::TextDisabled("No files found.");
        } else {
            drawFileRange(selected->files, 0, selected->files.size(), {});
        }
        ImGui::EndChild();
    }
}

bool mod_manager::init() {
    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) == 0) {
        sampo::log::error("Could not initialize Mod Manager: /arrow Windows error: %lu", GetLastError());
        return false;
    }

    const std::filesystem::path root = std::filesystem::path(executable).parent_path();
    p_modsDirectory = root / "mods";
    p_workshopDirectory = root.parent_path().parent_path() / "workshop" / "content" / "881100";
    p_configPath = configPath();
    refresh();
    const Snapshot current = snapshot();
    sampo::log::write("Mod Manager initialized: /arrow Mods: %zu /arrow Active: %zu /arrow Directory: %s", current.mods.size(), current.active.size(), p_modsDirectory.string().c_str());
    return current.error.empty();
}

void mod_manager::refresh() {
    std::scoped_lock lock(p_mutex);
    refreshLocked();
}

bool mod_manager::findMod(const std::string& sourceId, std::string& id, std::string& path) {
    const std::string wanted = lower(sourceId);
    std::scoped_lock lock(p_mutex);
    for (const Mod& mod : p_mods) {
        const std::string directoryName = std::filesystem::path(mod.directory).filename().string();
        if (lower(mod.id) == wanted || lower(directoryName) == wanted) {
            id = mod.id;
            path = mod.directory;
            return true;
        }
    }
    return false;
}

void mod_manager::draw(float top) {
    const Snapshot current = snapshot();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, displaySize.y - top), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##ModManager", nullptr, flags)) {
        if (ImGui::Button("Refresh")) {
            refresh();
        }
        if (!current.error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.24f, 0.22f, 1.0f), "%s", current.error.c_str());
        }

        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float listWidth = (availableWidth - gap * 2.0f) * 0.24f;
        ImGui::BeginChild("##InactivePanel", ImVec2(listWidth, -1.0f), true);
        drawInactive(current.mods);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##ActivePanel", ImVec2(listWidth, -1.0f), true);
        drawActive(current.active);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##DetailsPanel", ImVec2(-1.0f, -1.0f), true);
        drawDetails(current.active);
        ImGui::EndChild();
    }
    ImGui::End();
}
