#include <windows.h>
#include "lua51.h"
#include "mod_manager.h"
#include "noita_mainmenu.h"
#include "qol_wand_comparison.h"
#include "sdl2.h"
#include "settings.h"
#include "steam.h"
#include "wand.h"
#include "wand_support.h"
#include "log.h"

namespace {
    INIT_ONCE p_started = INIT_ONCE_STATIC_INIT;

    BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
        sampo::log::write("Initializing Sampo..");
        steam::init();
        settings::init();
        wand::init();
        lua51::init();
        qol_wand_comparison::init();
        mod_manager::init();
        sdl2::init();
        const bool wandLoaded = wand_support::load();
        noita_mainmenu::init(settings::noitaModCheck(), settings::useDefaultBuildText(), settings::useDefaultModsScreen(), !wandLoaded);
        return TRUE;
    }
}

extern "C" __declspec(dllexport) void __cdecl SampoStart() {
    InitOnceExecuteOnce(&p_started, initialize, nullptr, nullptr);
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
    }

    return TRUE;
}
