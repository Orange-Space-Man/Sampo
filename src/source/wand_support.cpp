#include "wand_support.h"

#include "log.h"
#include "noita_mainmenu.h"

#include <windows.h>

#include <filesystem>

namespace {
    using Start = void(__cdecl*)();
    using SetStartFilter = void(__cdecl*)(int(__cdecl*)(void*, void*));
    using ResumeStart = void(__cdecl*)(void*, void*);

    HMODULE p_module = nullptr;
    ResumeStart p_resumeStart = nullptr;

    int __cdecl filterStart(void* gameMode, void* startOptions) {
        return noita_mainmenu::deferNewGame(gameMode, startOptions, p_resumeStart) ? TRUE : FALSE;
    }
}

bool wand_support::load() {
    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) {
        return false;
    }

    const std::filesystem::path path = std::filesystem::path(executable).parent_path() / L"WANd.dll";
    if (!std::filesystem::exists(path)) {
        return false;
    }

    bool loadedHere = false;
    p_module = GetModuleHandleW(path.filename().c_str());
    if (p_module == nullptr) {
        p_module = LoadLibraryW(path.c_str());
        loadedHere = p_module != nullptr;
    }
    if (p_module == nullptr) {
        sampo::log::error("Could not load WANd.dll: /arrow Windows error: %lu", GetLastError());
        return false;
    }

    const Start start = reinterpret_cast<Start>(GetProcAddress(p_module, "WANdStart"));
    const SetStartFilter setStartFilter = reinterpret_cast<SetStartFilter>(GetProcAddress(p_module, "WANdSetStartFilter"));
    p_resumeStart = reinterpret_cast<ResumeStart>(GetProcAddress(p_module, "WANdResumeStart"));
    if (start == nullptr || setStartFilter == nullptr || p_resumeStart == nullptr) {
        sampo::log::error("WANd.dll does not expose the Sampo compatibility API");
        if (loadedHere) {
            FreeLibrary(p_module);
        }
        p_module = nullptr;
        p_resumeStart = nullptr;
        return false;
    }

    setStartFilter(&filterStart);
    sampo::log::write("WANd compatibility enabled");
    return true;
}
