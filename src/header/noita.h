#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace noita {
    const HMODULE noitaBase = GetModuleHandleW(nullptr);
    const HMODULE lua51Base = GetModuleHandleW(L"lua51.dll");

    constexpr std::uintptr_t modsClickHandlerRva = 0x002E4114;
    constexpr std::uintptr_t afterModsEntryRva = 0x002E4155;
    constexpr std::uintptr_t mainMenuHeartbeatRva = 0x002E406C;
    constexpr std::uintptr_t mainMenuFlagRva = 0x00E0761B;
    constexpr std::uintptr_t modsUsedSetterRva = 0x002B3AD5;
    constexpr std::uintptr_t gameStatePointerRva = 0x00E05010;
    constexpr std::uintptr_t refreshModsRva = 0x00437A10;
    constexpr std::size_t modsUsedFieldOffset = 0x120;
}
