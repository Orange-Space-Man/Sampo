#pragma once

#include <string>
#include <vector>

namespace steam {
    bool init();
    bool hasAchievement(const char* id);
    bool rewardAchievement(const char* id);
    bool removeAchievement(const char* id);
    std::vector<std::string> getAchievements();
}
