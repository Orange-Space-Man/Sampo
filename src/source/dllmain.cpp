#include <windows.h>
#include "lua51.h"
#include "noita_mainmenu.h"
#include "sdl2.h"
#include "log.h"

namespace {
    DWORD WINAPI Initialize(LPVOID) {
        sampo::log::write("Initializing Sampo..");
        lua51::init();
        sdl2::init();
        noita_mainmenu::init();
        return 0;
    }
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        const HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }

    return TRUE;
}