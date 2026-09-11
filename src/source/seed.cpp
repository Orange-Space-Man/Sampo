#include "seed.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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

        double randomUnit() {
            step();
            const float value = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(p_state)) * p_randomScale);
            return static_cast<double>(value);
        }

        double distribution(double minimum, double maximum, double mean, double sharpness) {
            if (sharpness == 0.0) {
                return minimum + randomUnit() * (maximum - minimum);
            }

            const float range = static_cast<float>(maximum - minimum);
            const float middle = static_cast<float>((mean - minimum) / (maximum - minimum));
            constexpr float pi = 3.141499996185302734375f;
            for (int index = 0; index < 100; ++index) {
                const float candidate = static_cast<float>(randomUnit());
                const float roll = static_cast<float>(randomUnit());
                const float distance = std::abs(candidate - middle);
                if ((1.0f - distance) * 0.005f > roll) {
                    return minimum + candidate * range;
                }
                if (distance < 0.5f) {
                    const float angle = (0.5f - middle + candidate) * pi;
                    const float sine = static_cast<float>(std::sin(static_cast<double>(angle)));
                    const float power = static_cast<float>(std::pow(static_cast<double>(sine), sharpness));
                    if (power >= roll) {
                        return minimum + candidate * range;
                    }
                }
            }
            return minimum + randomUnit() * range;
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

    enum class WandValue {
        reload,
        fireRate,
        spread,
        speed,
        capacity,
        shuffle,
        actions
    };

    struct WandRange {
        double probability;
        double minimum;
        double maximum;
        double mean;
        double sharpness;
    };

    struct Wand {
        double cost = 0.0;
        double capacity = 0.0;
        double actions = 0.0;
        double reload = 0.0;
        double shuffle = 1.0;
        double fireRate = 0.0;
        double spread = 0.0;
        double speed = 0.0;
        double manaCharge = 0.0;
        double manaMax = 0.0;
        double forceUnshuffle = 0.0;
        double rare = 0.0;
    };

    double clamp(double value, double minimum, double maximum) {
        if (minimum > maximum) {
            return clamp(value, maximum, minimum);
        }
        return (std::max)(minimum, (std::min)(maximum, value));
    }

    WandRange wandRange(WorldRandom& random, WandValue value) {
        constexpr std::array<WandRange, 7> capacity{{
            {1.0, 3.0, 10.0, 6.0, 2.0}, {0.1, 2.0, 7.0, 4.0, 4.0}, {0.05, 1.0, 5.0, 3.0, 4.0},
            {0.15, 5.0, 11.0, 8.0, 2.0}, {0.12, 2.0, 20.0, 8.0, 4.0}, {0.15, 3.0, 12.0, 6.0, 6.0},
            {1.0, 1.0, 20.0, 6.0, 0.0}
        }};
        constexpr std::array<WandRange, 4> reload{{
            {1.0, 5.0, 60.0, 30.0, 2.0}, {0.5, 1.0, 100.0, 40.0, 2.0},
            {0.02, 1.0, 100.0, 40.0, 0.0}, {0.35, 1.0, 240.0, 40.0, 0.0}
        }};
        constexpr std::array<WandRange, 4> fireRate{{
            {1.0, 1.0, 30.0, 5.0, 2.0}, {0.1, 1.0, 50.0, 15.0, 3.0},
            {0.1, -15.0, 15.0, 0.0, 3.0}, {0.45, 0.0, 35.0, 12.0, 0.0}
        }};
        constexpr std::array<WandRange, 2> spread{{
            {1.0, -5.0, 10.0, 0.0, 3.0}, {0.1, -35.0, 35.0, 0.0, 0.0}
        }};
        constexpr std::array<WandRange, 5> speed{{
            {1.0, 0.8, 1.2, 1.0, 6.0}, {0.05, 1.0, 2.0, 1.1, 3.0}, {0.05, 0.5, 1.0, 0.9, 3.0},
            {1.0, 0.8, 1.2, 1.0, 0.0}, {0.001, 1.0, 10.0, 5.0, 2.0}
        }};
        constexpr std::array<WandRange, 4> actions{{
            {1.0, 1.0, 3.0, 1.0, 3.0}, {0.2, 2.0, 4.0, 2.0, 8.0},
            {0.05, 1.0, 5.0, 2.0, 2.0}, {1.0, 1.0, 5.0, 2.0, 0.0}
        }};

        const WandRange* ranges = nullptr;
        std::size_t count = 0;
        if (value == WandValue::capacity) {
            ranges = capacity.data();
            count = capacity.size();
        } else if (value == WandValue::reload) {
            ranges = reload.data();
            count = reload.size();
        } else if (value == WandValue::fireRate) {
            ranges = fireRate.data();
            count = fireRate.size();
        } else if (value == WandValue::spread) {
            ranges = spread.data();
            count = spread.size();
        } else if (value == WandValue::speed) {
            ranges = speed.data();
            count = speed.size();
        } else {
            ranges = actions.data();
            count = actions.size();
        }

        double total = 0.0;
        for (std::size_t index = 0; index < count; ++index) {
            total += ranges[index].probability;
        }
        double roll = random.randomUnit() * total;
        for (std::size_t index = 0; index < count; ++index) {
            if (roll <= ranges[index].probability) {
                return ranges[index];
            }
            roll -= ranges[index].probability;
        }
        return ranges[count - 1];
    }

    void shuffleWandValues(WorldRandom& random, WandValue* values, std::size_t count) {
        for (int index = static_cast<int>(count) - 1; index >= 1; --index) {
            const int other = random.random(0, index);
            std::swap(values[index], values[other]);
        }
    }

    void applyWandValue(WorldRandom& random, Wand& wand, WandValue value) {
        const WandRange range = wandRange(random, value);
        if (value == WandValue::reload) {
            const double minimum = clamp(60.0 - wand.cost * 5.0, 1.0, 240.0);
            wand.reload = clamp(std::round(random.distribution(range.minimum, range.maximum, range.mean, range.sharpness)), minimum, 1024.0);
            wand.cost -= (60.0 - wand.reload) / 5.0;
        } else if (value == WandValue::fireRate) {
            const double minimum = clamp(16.0 - wand.cost, -50.0, 50.0);
            wand.fireRate = clamp(std::round(random.distribution(range.minimum, range.maximum, range.mean, range.sharpness)), minimum, 50.0);
            wand.cost -= 16.0 - wand.fireRate;
        } else if (value == WandValue::spread) {
            const double minimum = clamp(wand.cost / -1.5, -35.0, 35.0);
            wand.spread = clamp(std::round(random.distribution(range.minimum, range.maximum, range.mean, range.sharpness)), minimum, 35.0);
            wand.cost -= 16.0 - wand.spread;
        } else if (value == WandValue::speed) {
            wand.speed = random.distribution(range.minimum, range.maximum, range.mean, range.sharpness);
        } else if (value == WandValue::capacity) {
            double maximum = clamp(wand.cost / 5.0 + 6.0, 1.0, 20.0);
            if (wand.forceUnshuffle == 1.0) {
                maximum = (wand.cost - 15.0) / 5.0;
                if (maximum > 6.0) {
                    maximum = 6.0 + (wand.cost - 45.0) / 10.0;
                }
            }
            maximum = clamp(maximum, 1.0, 20.0);
            wand.capacity = clamp(std::round(random.distribution(range.minimum, range.maximum, range.mean, range.sharpness)), 1.0, maximum);
            wand.cost -= (wand.capacity - 6.0) * 5.0;
        } else if (value == WandValue::shuffle) {
            const int roll = random.random(0, 1);
            if ((roll == 1 || wand.forceUnshuffle == 1.0) && wand.cost >= 15.0 + wand.capacity * 5.0 && wand.capacity <= 9.0) {
                wand.shuffle = 0.0;
                wand.cost -= 15.0 + wand.capacity * 5.0;
            }
        } else if (value == WandValue::actions) {
            const std::array<double, 5> costs{0.0, 5.0 + wand.capacity * 2.0, 15.0 + wand.capacity * 3.5, 35.0 + wand.capacity * 5.0, 45.0 + wand.capacity * wand.capacity};
            int maximum = 1;
            for (std::size_t index = 0; index < costs.size(); ++index) {
                if (costs[index] <= wand.cost) {
                    maximum = static_cast<int>(index) + 1;
                }
            }
            maximum = static_cast<int>(clamp(maximum, 1.0, wand.capacity));
            wand.actions = std::floor(clamp(std::round(random.distribution(range.minimum, range.maximum, range.mean, range.sharpness)), 1.0, maximum));
            wand.cost -= costs[static_cast<std::size_t>(wand.actions) - 1];
        }
    }

    Wand makeWand(std::uint32_t seed, double x, double y, bool forceUnshuffle) {
        WorldRandom random(seed, x, y);
        double cost = 30.0;
        if (forceUnshuffle) {
            cost = 25.0;
        }
        if (random.random(0, 100) < 50) {
            cost += 5.0;
        }
        cost += random.random(-3, 3);

        Wand wand;
        wand.cost = cost;
        wand.manaCharge = 50.0 + random.random(-5, 5);
        wand.manaMax = 200.0 + random.random(-5, 5) * 10.0;
        if (random.random(0, 100) < 20) {
            wand.manaCharge = (50.0 + random.random(-5, 5)) / 5.0;
            wand.manaMax = (200.0 + random.random(-5, 5) * 10.0) * 3.0;
        }
        if (random.random(0, 100) < 15) {
            wand.manaCharge = (50.0 + random.random(-5, 5)) * 5.0;
            wand.manaMax = (200.0 + random.random(-5, 5) * 10.0) / 3.0;
        }
        wand.manaMax = (std::max)(wand.manaMax, 50.0);
        wand.manaCharge = (std::max)(wand.manaCharge, 10.0);
        if (random.random(0, 100) < 21) {
            wand.forceUnshuffle = 1.0;
        }
        if (random.random(0, 100) < 5) {
            wand.rare = 1.0;
            wand.cost += 65.0;
        }

        std::array<WandValue, 4> first{WandValue::reload, WandValue::fireRate, WandValue::spread, WandValue::speed};
        std::array<WandValue, 2> last{WandValue::shuffle, WandValue::actions};
        shuffleWandValues(random, first.data(), first.size());
        if (wand.forceUnshuffle != 1.0) {
            shuffleWandValues(random, last.data(), last.size());
        }
        for (const WandValue value : first) {
            applyWandValue(random, wand, value);
        }
        applyWandValue(random, wand, WandValue::capacity);
        for (const WandValue value : last) {
            applyWandValue(random, wand, value);
        }

        if (wand.cost > 5.0 && random.random(0, 1000) < 995) {
            if (wand.shuffle == 1.0) {
                wand.capacity += wand.cost / 5.0;
            } else {
                wand.capacity += wand.cost / 10.0;
            }
            wand.cost = 0.0;
        }
        if (forceUnshuffle) {
            wand.shuffle = 0.0;
        }
        if (random.random(0, 10000) <= 9999) {
            wand.capacity = clamp(wand.capacity, 2.0, 26.0);
        }
        wand.capacity = (std::max)(wand.capacity, 2.0);
        if (wand.reload >= 60.0) {
            do {
                wand.actions += 1.0;
            } while (random.random(0, 100) < 70);
            if (random.random(0, 100) < 50) {
                int newActions = static_cast<int>(wand.capacity);
                for (int index = 0; index < 6; ++index) {
                    const int value = random.random(static_cast<int>(wand.actions), static_cast<int>(wand.capacity));
                    newActions = (std::min)(newActions, value);
                }
                wand.actions = newActions;
            }
        }
        wand.actions = clamp(wand.actions, 1.0, wand.capacity);
        return wand;
    }

    int wandValue(const Wand& wand) {
        int score = static_cast<int>(wand.capacity * 2.0 + wand.manaMax / 40.0 + wand.manaCharge / 20.0);
        score -= static_cast<int>((std::max)(wand.reload, 0.0) / 8.0);
        score -= static_cast<int>((std::max)(wand.fireRate, 0.0) / 5.0);
        score -= static_cast<int>(std::abs(wand.spread) / 4.0);
        if (wand.shuffle == 0.0) {
            score += 18;
        }
        if (wand.rare != 0.0) {
            score += 20;
        }
        return score;
    }

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
                int best = (std::numeric_limits<int>::min)();
                int second = best;
                for (int wandIndex = 0; wandIndex < 5; ++wandIndex) {
                    const double rawX = -331.0 + static_cast<double>(wandIndex) * 26.4;
                    const double x = std::nearbyint(rawX);
                    WorldRandom shopRandom(seed, x, yPositions[index]);
                    const bool shuffle = shopRandom.random(0, 100) <= 50;
                    const Wand wand = makeWand(seed, x, yPositions[index], !shuffle);
                    const int value = wandValue(wand);
                    if (value > best) {
                        second = best;
                        best = value;
                    } else if (value > second) {
                        second = value;
                    }
                }
                if (best != (std::numeric_limits<int>::min)()) {
                    score += best / 3;
                }
                if (second != (std::numeric_limits<int>::min)()) {
                    score += second / 5;
                }
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

    std::uint32_t findSeed(bool good) {
        std::vector<Candidate> finalists;
        finalists.reserve(p_finalists);
        std::uint32_t candidateSeed = firstSeed();
        if (!good) {
            candidateSeed ^= 0xA5A5A5A5;
        }

        for (std::size_t index = 0; index < p_seedCount; ++index) {
            if (candidateSeed == 0) {
                candidateSeed = 1;
            }
            const Candidate candidate{candidateSeed, alchemyScore(candidateSeed)};
            if (finalists.size() < p_finalists) {
                finalists.push_back(candidate);
            } else {
                std::size_t replacement = 0;
                for (std::size_t finalist = 1; finalist < finalists.size(); ++finalist) {
                    if (good && finalists[finalist].score < finalists[replacement].score) {
                        replacement = finalist;
                    }
                    if (!good && finalists[finalist].score > finalists[replacement].score) {
                        replacement = finalist;
                    }
                }
                if (good && candidate.score > finalists[replacement].score) {
                    finalists[replacement] = candidate;
                }
                if (!good && candidate.score < finalists[replacement].score) {
                    finalists[replacement] = candidate;
                }
            }
            ++candidateSeed;
        }

        Candidate best;
        if (good) {
            best.score = (std::numeric_limits<int>::min)();
        } else {
            best.score = (std::numeric_limits<int>::max)();
        }
        for (Candidate candidate : finalists) {
            candidate.score += perkScore(candidate.seed);
            candidate.score += wandShopScore(candidate.seed);
            if (good && candidate.score > best.score) {
                best = candidate;
            }
            if (!good && candidate.score < best.score) {
                best = candidate;
            }
        }
        return best.seed;
    }
}

std::uint32_t seed::getGoodSeed() {
    return findSeed(true);
}

std::uint32_t seed::getBadSeed() {
    return findSeed(false);
}
