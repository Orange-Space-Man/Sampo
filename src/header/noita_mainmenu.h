#pragma once

#include <cstdint>
#include <string>

namespace noita_mainmenu {
    bool init(bool noitaModCheck, bool defaultBuildText, bool defaultModsScreen);
    bool setNoitaModCheck(bool enabled);
    bool setDefaultBuildText(bool enabled);
    bool setDefaultModsScreen(bool enabled);
    void setWorldSeed(std::uint32_t seed);
    void updateModCheck();
    const std::string& getSampoBuildText();
    void draw();
}
