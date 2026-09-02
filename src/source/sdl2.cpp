#include "sdl2.h"

#include "memory.h"
#include "noita.h"
#include "overlay.h"
#include "log.h"

namespace
{
    struct SDL_Window;
    using SdlGlSwapWindow = void(__cdecl*)(SDL_Window*);

    SdlGlSwapWindow f_oSwapWindow = nullptr;
    bool swapLogged = false;

    void __cdecl hookSwapWindow(SDL_Window* window) {
        overlay::draw();
        f_oSwapWindow(window);
        if (!swapLogged) {
            sampo::log::write("SDL_GL_SwapWindow hooked successfully: /arrow %p", reinterpret_cast<void*>(f_oSwapWindow));
            swapLogged = true;
        }
    }
}

bool sdl2::init()
{
    sampo::log::write("Initializing SDL2 hook..");
    if (f_oSwapWindow != nullptr) {
        return true;
    }

    return memory::hook_iat(noita::noitaBase, "SDL2.dll", "SDL_GL_SwapWindow", reinterpret_cast<void*>(&hookSwapWindow), reinterpret_cast<void**>(&f_oSwapWindow));
}
