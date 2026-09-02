#include "noita_mainmenu.h"

#include "log.h"
#include "memory.h"
#include "noita.h"
#include "overlay.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
    constexpr const char* p_buildTextPrefix = "Noita - Build ";
    constexpr const char* p_mainMenuAnchor = "$menu_mods";
    constexpr std::size_t p_maxRefDistance = 0x10000;
    std::string p_noitaDate;
    std::string p_sampoDate = __DATE__;
    std::string p_noitaTime;
    bool p_initialized = false;

    bool fixDate(std::string& date) {
        if (date.size() != 11 || date[3] != ' ' || date[6] != ' ') {
            return false;
        }

        if (date[4] == ' ') {
            date[4] = '0';
        }
        return true;
    }
}

extern "C" alignas(8) volatile LONG64 p_sampoMainMenuTick = 0;
extern "C" volatile std::uintptr_t p_sampoNativeBuildTextAddress = 0;

extern "C" void __cdecl markMainMenu() {
    InterlockedExchange64(&p_sampoMainMenuTick, static_cast<LONG64>(GetTickCount64()));
}

extern "C" __declspec(naked) void hookBuildText() {
    __asm {
        pushfd
        pushad
        call markMainMenu
        popad
        popfd
        push eax
        mov eax, dword ptr [p_sampoNativeBuildTextAddress]
        xchg eax, dword ptr [esp + 4]
        xchg eax, dword ptr [esp]
        ret
    }
}

bool noita_mainmenu::init() {
    sampo::log::write("Initializing Main Menu modifications..");
    if (noita::noitaBase == nullptr) {
        sampo::log::error("Could not modify main menu, noita base is null");
        return false;
    }

    sampo::log::write("Locating noita build text..");
    memory::StringRef buildRef;
    if (!memory::find_ref(noita::noitaBase, p_buildTextPrefix, false, p_mainMenuAnchor, true, p_maxRefDistance, buildRef)) {
        sampo::log::error("Could not locate the noita build text");
        return false;
    }

    const std::string buildText = buildRef.text;
    const std::size_t dateStart = std::string_view(p_buildTextPrefix).size();
    const std::size_t timeSplit = buildText.find(" - ", dateStart);
    if (timeSplit == std::string::npos) {
        sampo::log::error("Could not split noita build text");
        return false;
    }
    p_noitaDate = buildText.substr(dateStart, timeSplit - dateStart);
    if (!fixDate(p_noitaDate) || !fixDate(p_sampoDate)) {
        sampo::log::error("Could not split noita build date");
        return false;
    }
    p_noitaTime = buildText.substr(timeSplit + 3);
    p_sampoNativeBuildTextAddress = reinterpret_cast<std::uintptr_t>(buildRef.text);
    sampo::log::write("Build text found: /arrow Text: %s /arrow Date: %s /arrow Time: %s /arrow Address: %p", buildText.c_str(), p_noitaDate.c_str(), p_noitaTime.c_str(), p_sampoNativeBuildTextAddress);
    sampo::log::write("Hooking build text address..");
    if (!memory::write_call(buildRef.instruction, reinterpret_cast<const void*>(&hookBuildText))) {
        sampo::log::error("Error while hooking build text address: /arrow Windows error: %lu", GetLastError());
        return false;
    }

    sampo::log::write("Hiding original build text..");
    constexpr char hidden = '\0';
    if (!memory::write(buildRef.text, &hidden, sizeof(hidden))) {
        memory::write_push(buildRef.instruction, buildRef.text);
        sampo::log::error("Could not hide the original build text: /arrow Windows error: %lu", GetLastError());
        return false;
    }

    p_initialized = true;
    sampo::log::write("Main menu modifications successful");
    return true;
}

void noita_mainmenu::draw() {
    if (!p_initialized) {
        return;
    }

    const ULONGLONG currentTick = GetTickCount64();
    const ULONGLONG lastMainMenuTick = static_cast<ULONGLONG>(InterlockedCompareExchange64(&p_sampoMainMenuTick, 0, 0));
    const bool mainMenuVisible = lastMainMenuTick != 0 && currentTick - lastMainMenuTick < 250;
    if (!mainMenuVisible) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float calculatedScale = std::floor(displaySize.y / 360.0f);
    float nativeScale = calculatedScale;
    if (nativeScale < 1.0f) {
        nativeScale = 1.0f;
    }
    const float scale = nativeScale * 0.95f;
    const float x = 13.0f * nativeScale;
    const float sampoY = displaySize.y - 10.5f * nativeScale;
    const float noitaY = sampoY - 8.0f * nativeScale;
    const std::array<std::array<std::string, 7>, 2> lines{{
        {"Noita", "- Build", p_noitaDate.substr(0, 3), p_noitaDate.substr(4, 2), p_noitaDate.substr(7, 4), "-", p_noitaTime},
        {"Sampo", "- Build", p_sampoDate.substr(0, 3), p_sampoDate.substr(4, 2), p_sampoDate.substr(7, 4), "-", __TIME__}
    }};
    const std::array<float, 2> yPositions{noitaY, sampoY};
    const std::array<unsigned int, 2> colors{IM_COL32(0x3E, 0x3F, 0x46, 0xFF), IM_COL32_WHITE};
    std::array<float, 7> xPositions{};
    xPositions[0] = x;
    const float spaceWidth = overlay::noitaTextWidth(scale, " ");
    for (std::size_t column = 1; column < xPositions.size(); ++column) {
        const std::size_t previous = column - 1;
        const float width = (std::max)(overlay::noitaTextWidth(scale, lines[0][previous].c_str()), overlay::noitaTextWidth(scale, lines[1][previous].c_str()));
        xPositions[column] = xPositions[previous] + width + spaceWidth;
    }

    for (std::size_t row = 0; row < lines.size(); ++row) {
        for (std::size_t column = 0; column < lines[row].size(); ++column) {
            overlay::drawNoitaText(xPositions[column], yPositions[row], scale, colors[row], lines[row][column].c_str(), row == 1);
        }
    }
}
