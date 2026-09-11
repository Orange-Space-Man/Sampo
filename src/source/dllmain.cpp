#include <windows.h>
#include "lua51.h"
#include "mod_manager.h"
#include "noita_mainmenu.h"
#include "qol_wand_comparison.h"
#include "sdl2.h"
#include "settings.h"
#include "steam.h"
#include "wand.h"
#include "log.h"

namespace {
    DWORD WINAPI Initialize(LPVOID) {
        sampo::log::write("Initializing Sampo..");
        steam::init();
        settings::init();
        wand::init();
        lua51::init();
        qol_wand_comparison::init();
        mod_manager::init();
        sdl2::init();
        noita_mainmenu::init(settings::noitaModCheck(), settings::useDefaultBuildText(), settings::useDefaultModsScreen());
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
