#include "steam.h"=
#include <windows.h>
#include <cstdint>
#include "log.h"

namespace {
    using GetSteamUser = int(__cdecl*)();
    using FindUserInterface = void*(__cdecl*)(int, const char*);
    GetSteamUser p_getSteamUser = nullptr;
    FindUserInterface p_findUserInterface = nullptr;

    void* getUserStats() {
        if (p_getSteamUser == nullptr || p_findUserInterface == nullptr) {
            return nullptr;
        }
        return p_findUserInterface(p_getSteamUser(), "STEAMUSERSTATS_INTERFACE_VERSION011");
    }

    std::uint32_t getAchievementCount(void* userStats) {
        __try {
            void** const vtable = *reinterpret_cast<void***>(userStats);
            using GetAchievementCount = std::uint32_t(__thiscall*)(void*);
            return reinterpret_cast<GetAchievementCount>(vtable[14])(userStats);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    const char* getAchievementName(void* userStats, std::uint32_t index) {
        __try {
            void** const vtable = *reinterpret_cast<void***>(userStats);
            using GetAchievementName = const char*(__thiscall*)(void*, std::uint32_t);
            return reinterpret_cast<GetAchievementName>(vtable[15])(userStats, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }
}

bool steam::init() {
    sampo::log::write("Initializing steam hook..");
    HMODULE module = GetModuleHandleW(L"steam_api.dll");
    if(module == 0) {
        sampo::log::error("steam_api.dll not found / not loaded yet");
        return false;
    }

    p_getSteamUser = reinterpret_cast<GetSteamUser>(GetProcAddress(module, "SteamAPI_GetHSteamUser"));
    p_findUserInterface = reinterpret_cast<FindUserInterface>(GetProcAddress(module, "SteamInternal_FindOrCreateUserInterface"));
    sampo::log::write("Steam module found: /arrow steam_api.dll: %p /arrow User: %p /arrow Interface: %p", reinterpret_cast<void*>(&module), reinterpret_cast<void*>(&p_getSteamUser), reinterpret_cast<void*>(&p_findUserInterface));
    return p_getSteamUser != nullptr && p_findUserInterface != nullptr;
}

bool steam::hasAchievement(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return false;
    }

    void* const userStats = getUserStats();
    if (userStats == nullptr) {
        return false;
    }

    bool achieved = false;
    __try {
        void** const vtable = *reinterpret_cast<void***>(userStats);
        using GetAchievement = bool(__thiscall*)(void*, const char*, bool*);
        const bool found = reinterpret_cast<GetAchievement>(vtable[6])(userStats, id, &achieved);
        if (!found) {
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return achieved;
}

bool steam::rewardAchievement(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return false;
    }

    void* const userStats = getUserStats();
    if (userStats == nullptr) {
        return false;
    }

    __try {
        void** const vtable = *reinterpret_cast<void***>(userStats);
        using SetAchievement = bool(__thiscall*)(void*, const char*);
        using StoreStats = bool(__thiscall*)(void*);
        if (!reinterpret_cast<SetAchievement>(vtable[7])(userStats, id)) {
            return false;
        }
        return reinterpret_cast<StoreStats>(vtable[10])(userStats);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool steam::removeAchievement(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return false;
    }

    void* const userStats = getUserStats();
    if (userStats == nullptr) {
        return false;
    }

    __try {
        void** const vtable = *reinterpret_cast<void***>(userStats);
        using ClearAchievement = bool(__thiscall*)(void*, const char*);
        using StoreStats = bool(__thiscall*)(void*);
        if (!reinterpret_cast<ClearAchievement>(vtable[8])(userStats, id)) {
            return false;
        }
        return reinterpret_cast<StoreStats>(vtable[10])(userStats);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::vector<std::string> steam::getAchievements() {
    std::vector<std::string> achievements;
    void* const userStats = getUserStats();
    if (userStats == nullptr) {
        return achievements;
    }

    const std::uint32_t count = getAchievementCount(userStats);
    achievements.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const char* const id = getAchievementName(userStats, index);
        if (id != nullptr && id[0] != '\0') {
            achievements.emplace_back(id);
        }
    }
    return achievements;
}
