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
#include <cstring>
#include <string>
#include <string_view>

extern "C" void __cdecl markMainMenu();
extern "C" void hookMainMenuHeartbeat();

namespace {
    constexpr const char* p_buildTextPrefix = "Noita - Build ";
    constexpr const char* p_mainMenuAnchor = "$menu_mods";
    constexpr std::size_t p_maxRefDistance = 0x10000;
    std::string p_noitaDate;
    std::string p_sampoDate = __DATE__;
    std::string p_noitaTime;
    std::string p_sampoTime = __TIME__;
    std::string p_sampoBuildText;
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

    bool isFrontMenu() {
        __try {
            return *reinterpret_cast<const std::uint8_t*>(reinterpret_cast<std::uint8_t*>(noita::noitaBase) + noita::mainMenuFlagRva) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void __cdecl openModManager() {
        overlay::showModManager();
    }

    bool hookMainMenuFrame() {
        std::uint8_t* const heartbeat = reinterpret_cast<std::uint8_t*>(noita::noitaBase) + noita::mainMenuHeartbeatRva;
        constexpr std::array<std::uint8_t, 11> expected{0xC7, 0x84, 0x24, 0x38, 0x01, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00};
        if (std::memcmp(heartbeat, expected.data(), expected.size()) != 0) {
            sampo::log::error("Could not hook the main menu frame: /arrow Unsupported noita.exe build /arrow Address: %p", heartbeat);
            return false;
        }

        std::array<std::uint8_t, 11> replacement{0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const std::uintptr_t callback = reinterpret_cast<std::uintptr_t>(&hookMainMenuHeartbeat);
        const std::uintptr_t callbackReturn = reinterpret_cast<std::uintptr_t>(heartbeat + 5);
        const std::int32_t callbackDistance = static_cast<std::int32_t>(callback - callbackReturn);
        std::memcpy(replacement.data() + 1, &callbackDistance, sizeof(callbackDistance));
        if (!memory::write(heartbeat, replacement.data(), replacement.size())) {
            sampo::log::error("Could not hook the main menu frame: /arrow Windows error: %lu", GetLastError());
            return false;
        }
        sampo::log::write("Main menu frame hooked: /arrow Address: %p", heartbeat);
        return true;
    }

    bool hookModsButton() {
        std::uint8_t* const handler = reinterpret_cast<std::uint8_t*>(noita::noitaBase) + noita::modsClickHandlerRva;
        constexpr std::array<std::uint8_t, 7> expected{0x80, 0x7C, 0x24, 0x3C, 0x00, 0x74, 0x3A};
        if (std::memcmp(handler, expected.data(), expected.size()) != 0 || handler[7] != 0xA1 || handler[12] != 0xB9) {
            sampo::log::error("Could not hook the Mods button: /arrow Unsupported noita.exe build /arrow Address: %p", handler);
            return false;
        }

        std::array<std::uint8_t, 17> replacement{0x80, 0x7C, 0x24, 0x3C, 0x00, 0x74, 0x3A, 0xE8, 0, 0, 0, 0, 0xE9, 0, 0, 0, 0};
        const std::uintptr_t callback = reinterpret_cast<std::uintptr_t>(&openModManager);
        const std::uintptr_t callbackReturn = reinterpret_cast<std::uintptr_t>(handler + 12);
        const std::int32_t callbackDistance = static_cast<std::int32_t>(callback - callbackReturn);
        std::memcpy(replacement.data() + 8, &callbackDistance, sizeof(callbackDistance));

        const std::uintptr_t entryEnd = reinterpret_cast<std::uintptr_t>(noita::noitaBase) + noita::afterModsEntryRva;
        const std::uintptr_t jumpReturn = reinterpret_cast<std::uintptr_t>(handler + replacement.size());
        const std::int32_t jumpDistance = static_cast<std::int32_t>(entryEnd - jumpReturn);
        std::memcpy(replacement.data() + 13, &jumpDistance, sizeof(jumpDistance));
        if (!memory::write(handler, replacement.data(), replacement.size())) {
            sampo::log::error("Could not hook the Mods button: /arrow Windows error: %lu", GetLastError());
            return false;
        }

        sampo::log::write("Mods button hooked: /arrow Address: %p", handler);
        return true;
    }

    bool removeModRestrictions() {
        std::uint8_t* const setter = reinterpret_cast<std::uint8_t*>(noita::noitaBase) + noita::modsUsedSetterRva;
        constexpr std::array<std::uint8_t, 6> expected{0xC6, 0x80, 0x20, 0x01, 0x00, 0x00};
        if (std::memcmp(setter, expected.data(), expected.size()) != 0 || (setter[6] != 0x00 && setter[6] != 0x01)) {
            sampo::log::error("Could not remove Noita mod restrictions: /arrow Unsupported noita.exe build /arrow Address: %p", setter);
            return false;
        }

        constexpr std::uint8_t unrestricted = 0;
        if (!memory::write(setter + 6, &unrestricted, sizeof(unrestricted))) {
            sampo::log::error("Could not remove Noita mod restrictions: /arrow Windows error: %lu", GetLastError());
            return false;
        }

        sampo::log::write("Noita mod restrictions removed: /arrow Address: %p", setter);
        return true;
    }
}

extern "C" alignas(8) volatile LONG64 p_sampoMainMenuTick = 0;
extern "C" volatile std::uintptr_t p_sampoNativeBuildTextAddress = 0;

extern "C" void __cdecl markMainMenu() {
    InterlockedExchange64(&p_sampoMainMenuTick, static_cast<LONG64>(GetTickCount64()));
}

extern "C" __declspec(naked) void hookMainMenuHeartbeat() {
    __asm {
        mov dword ptr [esp + 13Ch], 0Fh
        pushfd
        pushad
        call markMainMenu
        popad
        popfd
        ret
    }
}

extern "C" __declspec(naked) void hookBuildText() {
    __asm {
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

    const bool modsButtonHooked = hookModsButton();
    const bool mainMenuFrameHooked = hookMainMenuFrame();
    const bool modRestrictionsRemoved = removeModRestrictions();

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
    p_sampoBuildText = "Sampo - Build " + p_sampoDate + " - " + p_sampoTime;
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
    return modsButtonHooked && mainMenuFrameHooked && modRestrictionsRemoved;
}

void noita_mainmenu::keepModsUnrestricted() {
    if (noita::noitaBase == nullptr) {
        return;
    }

    __try {
        const std::uintptr_t gameState = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uint8_t*>(noita::noitaBase) + noita::gameStatePointerRva);
        if (gameState != 0) {
            *reinterpret_cast<std::uint8_t*>(gameState + noita::modsUsedFieldOffset) = 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

const std::string& noita_mainmenu::getSampoBuildText() {
    if (p_sampoBuildText.empty()) {
        fixDate(p_sampoDate);
        p_sampoBuildText = "Sampo - Build " + p_sampoDate + " - " + p_sampoTime;
    }
    return p_sampoBuildText;
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

    if (!isFrontMenu()) {
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
        {"Sampo", "- Build", p_sampoDate.substr(0, 3), p_sampoDate.substr(4, 2), p_sampoDate.substr(7, 4), "-", p_sampoTime}
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
