#pragma once

#include <string>

namespace mod_manager {
    bool init();
    void refresh();
    bool findMod(const std::string& sourceId, std::string& id, std::string& path);
    bool beginNewGame();
    bool stepNewGame();
    void releaseNewGame();
    void draw(float top);
}
