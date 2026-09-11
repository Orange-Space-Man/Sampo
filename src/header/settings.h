#pragma once

#include <imgui.h>

namespace settings {
    bool init();
    bool noitaModCheck();
    bool useDefaultBuildText();
    bool useDefaultModsScreen();
    bool spellDescriptions();
    bool wandComparison();
    void applyTheme();
    void draw(float top);
    const ImVec4& guiDangerColor();
    const ImVec4& logTextColor();
    const ImVec4& logValueColor();
    const ImVec4& logArrowColor();
    const ImVec4& logErrorColor();
    const ImVec4& logErrorArrowColor();
}
