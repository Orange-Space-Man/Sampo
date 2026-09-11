#include "debug.h"

#include "log.h"

#include <windows.h>
#include <imgui.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    struct WakEntry {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    constexpr const char* p_wandPath = "data/items_gfx/wands/";
    std::atomic_bool p_exporting = false;

    bool getNoitaPath(std::filesystem::path& path) {
        wchar_t executable[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) {
            return false;
        }
        path = std::filesystem::path(executable).parent_path();
        return !path.empty();
    }

    bool readNumber(std::ifstream& file, std::uint32_t& value) {
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        return file.good();
    }

    bool readEntries(std::ifstream& file, std::vector<WakEntry>& entries, std::string& error) {
        file.seekg(4, std::ios::beg);
        std::uint32_t count = 0;
        if (!readNumber(file, count) || count > 1000000) {
            error = "The data.wak header is invalid";
            return false;
        }

        file.seekg(16, std::ios::beg);
        for (std::uint32_t index = 0; index < count; index++) {
            WakEntry entry;
            std::uint32_t nameLength = 0;
            if (!readNumber(file, entry.offset) || !readNumber(file, entry.size) || !readNumber(file, nameLength) || nameLength > 32768) {
                error = "The data.wak file table is invalid";
                return false;
            }

            entry.name.resize(nameLength);
            file.read(entry.name.data(), nameLength);
            if (!file.good()) {
                error = "Could not read the data.wak file table";
                return false;
            }
            if (entry.name.starts_with(p_wandPath) && entry.name.ends_with(".png")) {
                entries.push_back(std::move(entry));
            }
        }
        return true;
    }

    bool exportWandGfx(std::string& path, std::size_t& count, std::string& error) {
        std::filesystem::path noitaPath;
        if (!getNoitaPath(noitaPath)) {
            error = "Could not find Noita's path";
            return false;
        }

        const std::filesystem::path wakPath = noitaPath / "data" / "data.wak";
        const std::filesystem::path exportPath = noitaPath / "sampo" / "exports" / "wand_gfx";
        std::ifstream wak(wakPath, std::ios::binary);
        if (!wak.is_open()) {
            error = "Could not open " + wakPath.string();
            return false;
        }

        std::vector<WakEntry> entries;
        if (!readEntries(wak, entries, error)) {
            return false;
        }

        std::error_code directoryError;
        std::filesystem::create_directories(exportPath, directoryError);
        if (directoryError) {
            error = directoryError.message();
            return false;
        }

        std::vector<char> data;
        for (const WakEntry& entry : entries) {
            data.resize(entry.size);
            wak.clear();
            wak.seekg(entry.offset, std::ios::beg);
            wak.read(data.data(), entry.size);
            if (!wak.good()) {
                error = "Could not read " + entry.name;
                return false;
            }

            const std::filesystem::path filename = std::filesystem::path(entry.name).filename();
            std::ofstream output(exportPath / filename, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                error = "Could not write " + (exportPath / filename).string();
                return false;
            }
            output.write(data.data(), entry.size);
            if (!output.good()) {
                error = "Could not write " + (exportPath / filename).string();
                return false;
            }
            count++;
        }

        path = exportPath.string();
        return true;
    }

    void startExport() {
        if (p_exporting.exchange(true)) {
            return;
        }

        std::thread([]() {
            std::string path;
            std::string error;
            std::size_t count = 0;
            sampo::log::write("Exporting wand graphics..");
            if (exportWandGfx(path, count, error)) {
                sampo::log::write("Exported wand graphics: /arrow Path: %s /arrow Files: %zu", path.c_str(), count);
            } else {
                sampo::log::error("Could not export wand graphics: /arrow Error: %s", error.c_str());
            }
            p_exporting = false;
        }).detach();
    }
}

void debug::drawMenu() {
    if (!ImGui::BeginMenu("Debug")) {
        return;
    }

    if (p_exporting) {
        ImGui::MenuItem("Exporting Wand Gfx..", nullptr, false, false);
    } else if (ImGui::MenuItem("Export Wand Gfx")) {
        startExport();
    }
    ImGui::EndMenu();
}
