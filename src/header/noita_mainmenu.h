#pragma once

#include <cstdint>
#include <string>

namespace noita_mainmenu {
    using ContinueNewGame = void(__cdecl*)(void*, void*);

    bool init(bool noitaModCheck, bool defaultBuildText, bool defaultModsScreen, bool hookNewGame = true);
    bool deferNewGame(void* gameMode, void* startOptions, ContinueNewGame continuation);
    bool setNoitaModCheck(bool enabled);
    bool setDefaultBuildText(bool enabled);
    bool setDefaultModsScreen(bool enabled);
    void setWorldSeed(std::uint32_t seed);
    void updateModCheck();
    const std::string& getSampoBuildText();
    void draw();
}
