#include "seed.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <string_view>
#include <vector>

namespace {
    constexpr double p_randomScale = 4.656612875e-10;
    constexpr std::uint32_t p_randomMax = 0x7FFFFFFF;
    constexpr std::size_t p_seedCount = 75000;
    constexpr std::size_t p_finalists = 192;

    struct Candidate {
        std::uint32_t seed = 0;
        int score = 0;
    };

    struct WorseCandidate {
        bool operator()(const Candidate& left, const Candidate& right) const {
            return left.score > right.score;
        }
    };

    struct Perk {
        const char* id;
        int stackable;
        int maxInPool;
        int rare;
        int distance;
    };

    constexpr std::array<int, 22> p_liquidScores{
        0, 2, 2, 0, 1, 1, 0, 0, 3, 5, 4, 4, 6, 8, 8, 6, 6, 7, 8, 3, 3, 3
    };

    constexpr std::array<int, 18> p_materialScores{
        1, 4, 0, 2, 1, 1, 3, 4, 6, 6, 4, 5, 8, 0, 2, 4, 0, 2
    };

    constexpr std::array<Perk, 103> p_perks{{
        { "CRITICAL_HIT", 1, 0, 0, 0 },
        { "BREATH_UNDERWATER", 1, 0, 1, 0 },
        { "EXTRA_MONEY", 1, 0, 0, 0 },
        { "EXTRA_MONEY_TRICK_KILL", 1, 0, 0, 0 },
        { "GOLD_IS_FOREVER", 0, 0, 0, 0 },
        { "TRICK_BLOOD_MONEY", 0, 0, 0, 0 },
        { "EXPLODING_GOLD", 1, 1, 1, 0 },
        { "HOVER_BOOST", 1, 1, 0, 0 },
        { "FASTER_LEVITATION", 1, 1, 0, 0 },
        { "MOVEMENT_FASTER", 1, 2, 0, 0 },
        { "STRONG_KICK", 1, 1, 0, 0 },
        { "TELEKINESIS", 0, 0, 0, 0 },
        { "REPELLING_CAPE", 1, 2, 1, 0 },
        { "EXPLODING_CORPSES", 0, 0, 0, 0 },
        { "SAVING_GRACE", 0, 0, 0, 0 },
        { "INVISIBILITY", 0, 0, 0, 0 },
        { "GLOBAL_GORE", 1, 1, 0, 0 },
        { "REMOVE_FOG_OF_WAR", 0, 0, 0, 0 },
        { "LEVITATION_TRAIL", 1, 2, 1, 0 },
        { "VAMPIRISM", 0, 0, 0, 0 },
        { "EXTRA_HP", 1, 3, 0, 0 },
        { "HEARTS_MORE_EXTRA_HP", 1, 2, 1, 0 },
        { "GLASS_CANNON", 1, 2, 1, 0 },
        { "LOW_HP_DAMAGE_BOOST", 1, 2, 1, 0 },
        { "RESPAWN", 1, 0, 1, 0 },
        { "WORM_ATTRACTOR", 1, 0, 1, 0 },
        { "RADAR_ENEMY", 0, 0, 0, 0 },
        { "FOOD_CLOCK", 0, 0, 0, 0 },
        { "IRON_STOMACH", 0, 0, 0, 0 },
        { "WAND_RADAR", 0, 0, 0, 0 },
        { "ITEM_RADAR", 0, 0, 0, 0 },
        { "PROTECTION_FIRE", 0, 0, 0, 0 },
        { "PROTECTION_RADIOACTIVITY", 0, 0, 0, 0 },
        { "PROTECTION_EXPLOSION", 0, 0, 0, 0 },
        { "PROTECTION_MELEE", 0, 0, 0, 0 },
        { "PROTECTION_ELECTRICITY", 0, 0, 0, 0 },
        { "TELEPORTITIS", 0, 0, 0, 0 },
        { "TELEPORTITIS_DODGE", 0, 0, 0, 0 },
        { "STAINLESS_ARMOUR", 1, 0, 1, 0 },
        { "EDIT_WANDS_EVERYWHERE", 0, 0, 0, 0 },
        { "NO_WAND_EDITING", 0, 0, 0, 0 },
        { "WAND_EXPERIMENTER", 1, 0, 1, 0 },
        { "ADVENTURER", 0, 0, 0, 0 },
        { "ABILITY_ACTIONS_MATERIALIZED", 0, 0, 0, 0 },
        { "PROJECTILE_HOMING", 0, 0, 0, 0 },
        { "PROJECTILE_HOMING_SHOOTER", 0, 0, 0, 0 },
        { "UNLIMITED_SPELLS", 0, 0, 0, 0 },
        { "FREEZE_FIELD", 0, 0, 0, 0 },
        { "FIRE_GAS", 0, 0, 0, 0 },
        { "DISSOLVE_POWDERS", 0, 0, 0, 0 },
        { "BLEED_SLIME", 1, 0, 1, 0 },
        { "BLEED_OIL", 0, 0, 0, 0 },
        { "BLEED_GAS", 0, 0, 0, 0 },
        { "SHIELD", 1, 2, 0, 10 },
        { "REVENGE_EXPLOSION", 1, 0, 1, 0 },
        { "REVENGE_TENTACLE", 1, 0, 1, 0 },
        { "REVENGE_RATS", 0, 0, 0, 0 },
        { "REVENGE_BULLET", 1, 0, 1, 0 },
        { "ATTACK_FOOT", 1, 2, 1, 0 },
        { "PLAGUE_RATS", 1, 2, 1, 0 },
        { "VOMIT_RATS", 0, 0, 0, 0 },
        { "CORDYCEPS", 0, 0, 0, 0 },
        { "MOLD", 0, 0, 0, 0 },
        { "WORM_SMALLER_HOLES", 0, 0, 0, 0 },
        { "PROJECTILE_REPULSION", 1, 0, 1, 0 },
        { "RISKY_CRITICAL", 1, 2, 1, 0 },
        { "FUNGAL_DISEASE", 1, 2, 1, 0 },
        { "PROJECTILE_SLOW_FIELD", 1, 0, 1, 0 },
        { "PROJECTILE_REPULSION_SECTOR", 1, 0, 1, 0 },
        { "PROJECTILE_EATER_SECTOR", 0, 0, 0, 0 },
        { "ORBIT", 1, 2, 0, 10 },
        { "ANGRY_GHOST", 1, 0, 0, 0 },
        { "HUNGRY_GHOST", 1, 2, 0, 0 },
        { "DEATH_GHOST", 1, 0, 1, 0 },
        { "HOMUNCULUS", 1, 2, 0, 0 },
        { "LUKKI_MINION", 0, 0, 0, 0 },
        { "ELECTRICITY", 0, 0, 0, 0 },
        { "ATTRACT_ITEMS", 1, 1, 0, 0 },
        { "EXTRA_KNOCKBACK", 1, 0, 1, 0 },
        { "LOWER_SPREAD", 1, 0, 1, 0 },
        { "LOW_RECOIL", 0, 0, 0, 0 },
        { "BOUNCE", 0, 0, 0, 0 },
        { "FAST_PROJECTILES", 0, 0, 0, 0 },
        { "ALWAYS_CAST", 1, 0, 0, 0 },
        { "EXTRA_MANA", 1, 0, 0, 0 },
        { "NO_MORE_SHUFFLE", 0, 0, 0, 0 },
        { "NO_MORE_KNOCKBACK", 0, 0, 0, 0 },
        { "DUPLICATE_PROJECTILE", 1, 0, 0, 0 },
        { "FASTER_WANDS", 1, 0, 0, 0 },
        { "EXTRA_SLOTS", 1, 0, 0, 0 },
        { "CONTACT_DAMAGE", 0, 0, 0, 0 },
        { "EXTRA_PERK", 1, 3, 0, 0 },
        { "PERKS_LOTTERY", 1, 3, 1, 0 },
        { "GAMBLE", 1, 0, 0, 0 },
        { "EXTRA_SHOP_ITEM", 1, 2, 0, 0 },
        { "GENOME_MORE_HATRED", 1, 0, 0, 0 },
        { "GENOME_MORE_LOVE", 1, 0, 0, 0 },
        { "PEACE_WITH_GODS", 0, 0, 0, 0 },
        { "MANA_FROM_KILLS", 0, 0, 0, 0 },
        { "ANGRY_LEVITATION", 0, 0, 0, 0 },
        { "LASER_AIM", 1, 0, 1, 0 },
        { "PERSONAL_LASER", 1, 2, 1, 0 },
        { "MEGA_BEAM_STONE", 1, 0, 0, 0 }
    }};

    class AlchemyRandom {
    public:
        explicit AlchemyRandom(double seed) : p_seed(seed) {
            next();
        }

        double next() {
            const std::int32_t integerSeed = static_cast<std::int32_t>(p_seed);
            std::uint32_t first = static_cast<std::uint32_t>(integerSeed);
            first *= 16807;
            std::int32_t second = integerSeed / 127773;
            std::uint32_t secondBits = static_cast<std::uint32_t>(second);
            secondBits *= static_cast<std::uint32_t>(-static_cast<std::int32_t>(p_randomMax));
            std::int32_t result = static_cast<std::int32_t>(first + secondBits);
            if (result < 0) {
                result += static_cast<std::int32_t>(p_randomMax);
            }
            p_seed = result;
            return p_seed / static_cast<double>(p_randomMax);
        }

    private:
        double p_seed;
    };

    class WorldRandom {
    public:
        WorldRandom(std::uint32_t seed, double x, double y) {
            const std::uint32_t salt = seed ^ 0x93262E6F;
            const double adjustedX = x + static_cast<double>(salt & 0xFFF);
            const double adjustedY = y + static_cast<double>((salt >> 12) & 0xFFF);
            const std::int32_t first = low32(std::trunc(adjustedX * 134217727.0));
            double secondValue = adjustedY * 134217727.0;
            if (std::abs(adjustedY) < 102400.0 && std::abs(adjustedX) > 1.0) {
                secondValue = (adjustedY * 3483.328 + static_cast<double>(static_cast<std::uint32_t>(first))) * adjustedY;
            }
            const std::int32_t second = low32(std::trunc(secondValue));
            const std::uint32_t mixed = mix(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(second), seed);
            constexpr std::array<std::uint32_t, 4> tail{16807, 282475249, 1622650073, 984943658};
            p_state = reduce(static_cast<std::uint64_t>(diddle(mixed)) * tail[seed & 3]);
        }

        int random(int minimum, int maximum) {
            step();
            const std::int32_t range = static_cast<std::int32_t>(static_cast<std::uint32_t>(maximum - minimum) + 1);
            const double value = static_cast<double>(range) * (static_cast<double>(static_cast<std::int32_t>(p_state)) * -p_randomScale);
            const std::int32_t converted = low32(std::trunc(value));
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(minimum) - static_cast<std::uint32_t>(converted));
        }

    private:
        std::uint32_t p_state = 1;

        static std::int32_t low32(double value) {
            const std::int64_t converted = static_cast<std::int64_t>(value);
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(converted));
        }

        static std::uint32_t reduce(std::uint64_t value) {
            constexpr std::uint64_t divisor = 0x7FFFFFFF;
            value = (value & divisor) + (value >> 31);
            value = (value & divisor) + (value >> 31);
            if (value >= divisor) {
                value -= divisor;
            }
            return static_cast<std::uint32_t>(value);
        }

        static std::uint32_t mix(std::uint32_t first, std::uint32_t second, std::uint32_t third) {
            first -= second;
            first -= third;
            first ^= third >> 13;
            second -= first;
            second -= third;
            second ^= first << 8;
            third -= first;
            third -= second;
            third ^= second >> 13;
            first -= second;
            first -= third;
            first ^= third >> 12;
            second -= first;
            second -= third;
            second ^= first << 16;
            third -= first;
            third -= second;
            third ^= second >> 5;
            first -= second;
            first -= third;
            first ^= third >> 3;
            second -= first;
            second -= third;
            second ^= first << 10;
            third -= first;
            third -= second;
            third ^= second >> 15;
            return third;
        }

        static std::uint32_t diddle(std::uint32_t value) {
            constexpr std::array<std::uint32_t, 17> thresholds{0, 4, 6, 25, 12, 39, 52, 9, 21, 64, 78, 92, 104, 118, 18, 32, 44};
            constexpr std::uint32_t divisor = 252645135;
            constexpr std::uint32_t low = 0xC3C3C3C7;
            std::uint32_t result = value;
            if (value < 0x80000000) {
                ++result;
            }
            if (value == 0) {
                ++result;
            }
            const std::uint32_t index = value / divisor;
            const std::uint32_t remainder = value - index * divisor;
            result -= index;
            if (remainder < thresholds[index] && value - low >= 58) {
                ++result;
            }
            if (value > 0x80000000) {
                ++result;
            }
            result >>= 1;
            if (value == 0xFFFFFFFF) {
                ++result;
            }
            return result;
        }

        void step() {
            const std::uint64_t product = static_cast<std::uint64_t>(p_state) * 16807;
            p_state = static_cast<std::uint32_t>(product % p_randomMax);
            if (p_state == 0) {
                p_state = p_randomMax;
            }
        }
    };

    int recipeScore(AlchemyRandom& random, std::uint32_t seed) {
        std::array<int, 4> values{};
        std::array<int, 3> liquidIndexes{-1, -1, -1};
        for (std::size_t index = 0; index < liquidIndexes.size(); ++index) {
            while (true) {
                const int picked = static_cast<int>(random.next() * p_liquidScores.size());
                bool duplicate = false;
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (liquidIndexes[previous] == picked) {
                        duplicate = true;
                    }
                }
                if (!duplicate) {
                    liquidIndexes[index] = picked;
                    values[index] = p_liquidScores[picked];
                    break;
                }
            }
        }

        const int material = static_cast<int>(random.next() * p_materialScores.size());
        values[3] = p_materialScores[material];
        const double probability = random.next();
        random.next();

        const std::int32_t shifted = static_cast<std::int32_t>(seed) >> 1;
        AlchemyRandom shuffle(static_cast<double>(shifted + 0x30F6));
        for (int index = 3; index >= 0; --index) {
            const int other = static_cast<int>(shuffle.next() * static_cast<double>(index + 1));
            std::swap(values[index], values[other]);
        }

        const int total = values[0] + values[1] + values[2];
        const int hardest = (std::max)(values[0], (std::max)(values[1], values[2]));
        const int chance = 10 - static_cast<int>(probability * -91.0);
        return 160 - total * 14 - hardest * 7 + (100 - chance) / 8;
    }

    int alchemyScore(std::uint32_t seed) {
        AlchemyRandom random(static_cast<double>(seed) * 0.17127000 + 1323.59030000);
        for (int index = 0; index < 5; ++index) {
            random.next();
        }
        const int lively = recipeScore(random, seed);
        const int precursor = recipeScore(random, seed);
        return lively + precursor;
    }

    int perkValue(std::string_view id) {
        constexpr std::array<std::pair<std::string_view, int>, 35> values{{
            { "EDIT_WANDS_EVERYWHERE", 26 }, { "NO_MORE_SHUFFLE", 22 }, { "HEARTS_MORE_EXTRA_HP", 20 },
            { "EXTRA_HP", 16 }, { "EXTRA_MONEY", 16 }, { "GOLD_IS_FOREVER", 15 },
            { "PERKS_LOTTERY", 18 }, { "EXTRA_PERK", 17 }, { "EXTRA_SHOP_ITEM", 13 },
            { "UNLIMITED_SPELLS", 17 }, { "SAVING_GRACE", 15 }, { "SHIELD", 14 },
            { "EXTRA_MANA", 12 }, { "FASTER_WANDS", 12 }, { "ALWAYS_CAST", 11 },
            { "LOWER_SPREAD", 10 }, { "PROJECTILE_REPULSION", 10 }, { "PROJECTILE_SLOW_FIELD", 9 },
            { "WAND_EXPERIMENTER", 9 }, { "MANA_FROM_KILLS", 9 }, { "PEACE_WITH_GODS", 8 },
            { "PROTECTION_EXPLOSION", 8 }, { "PROTECTION_FIRE", 7 }, { "PROTECTION_ELECTRICITY", 7 },
            { "WAND_RADAR", 7 }, { "ITEM_RADAR", 6 }, { "REMOVE_FOG_OF_WAR", 6 },
            { "CRITICAL_HIT", 6 }, { "MOVEMENT_FASTER", 6 }, { "IRON_STOMACH", 5 },
            { "NO_WAND_EDITING", -22 }, { "TELEPORTITIS", -15 }, { "WORM_ATTRACTOR", -8 },
            { "PERSONAL_LASER", -7 }, { "GLASS_CANNON", -5 }
        }};
        for (const auto& value : values) {
            if (value.first == id) {
                return value.second;
            }
        }
        return 0;
    }

    int perkScore(std::uint32_t seed) {
        WorldRandom random(seed, 1.0, 2.0);
        std::vector<const Perk*> deck;
        deck.reserve(160);
        for (const Perk& perk : p_perks) {
            int count = 1;
            if (perk.stackable != 0) {
                int maximum = random.random(1, 2);
                if (perk.maxInPool != 0) {
                    maximum = random.random(1, perk.maxInPool);
                }
                if (perk.rare != 0) {
                    maximum = 1;
                }
                count = random.random(1, maximum);
            }
            for (int index = 0; index < count; ++index) {
                deck.push_back(&perk);
            }
        }

        for (int index = static_cast<int>(deck.size()) - 1; index >= 1; --index) {
            const int other = random.random(0, index);
            std::swap(deck[index], deck[other]);
        }

        for (int index = static_cast<int>(deck.size()) - 1; index >= 0; --index) {
            const Perk* const perk = deck[index];
            if (perk->stackable == 0) {
                continue;
            }
            int distance = perk->distance;
            if (distance == 0) {
                distance = 4;
            }
            bool remove = false;
            for (int previous = index - distance; previous < index; ++previous) {
                if (previous >= 0 && std::strcmp(deck[previous]->id, perk->id) == 0) {
                    remove = true;
                    break;
                }
            }
            if (remove) {
                deck.erase(deck.begin() + index);
            }
        }

        int score = 0;
        const std::size_t count = (std::min)(deck.size(), static_cast<std::size_t>(18));
        for (std::size_t index = 0; index < count; ++index) {
            int weight = 6 - static_cast<int>(index / 3);
            if (weight < 1) {
                weight = 1;
            }
            score += perkValue(deck[index]->id) * weight;
        }
        return score;
    }

    int wandShopScore(std::uint32_t seed) {
        constexpr std::array<double, 4> yPositions{1395.0, 2931.0, 4979.0, 6515.0};
        int score = 0;
        for (std::size_t index = 0; index < yPositions.size(); ++index) {
            WorldRandom random(seed, -331.0, yPositions[index]);
            random.random(0, 4);
            const bool wands = random.random(0, 100) > 50;
            if (wands) {
                score += 18 - static_cast<int>(index) * 3;
            }
        }
        return score;
    }

    std::uint32_t firstSeed() {
        const std::uint64_t clock = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::uint64_t value = clock + 0x9E3779B97F4A7C15;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EB;
        value ^= value >> 31;
        std::uint32_t result = static_cast<std::uint32_t>(value);
        if (result == 0) {
            result = 1;
        }
        return result;
    }
}

std::uint32_t seed::getGoodSeed() {
    std::priority_queue<Candidate, std::vector<Candidate>, WorseCandidate> finalists;
    std::uint32_t candidateSeed = firstSeed();
    for (std::size_t index = 0; index < p_seedCount; ++index) {
        if (candidateSeed == 0) {
            candidateSeed = 1;
        }
        const Candidate candidate{candidateSeed, alchemyScore(candidateSeed)};
        if (finalists.size() < p_finalists) {
            finalists.push(candidate);
        } else if (candidate.score > finalists.top().score) {
            finalists.pop();
            finalists.push(candidate);
        }
        ++candidateSeed;
    }

    Candidate best;
    best.score = (std::numeric_limits<int>::min)();
    while (!finalists.empty()) {
        Candidate candidate = finalists.top();
        finalists.pop();
        candidate.score += perkScore(candidate.seed);
        candidate.score += wandShopScore(candidate.seed);
        if (candidate.score > best.score) {
            best = candidate;
        }
    }
    return best.seed;
}
