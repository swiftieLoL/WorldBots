#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace Helper
{
    // Compare source travel with the expected number of loot attempts. Forty
    // yards per expected attempt is deliberately conservative: a nearby
    // mediocre source still wins, but a very low drop rate no longer beats a
    // substantially better camp merely because it is a few yards closer.
    inline float ScoreLootSource(float distanceSq, float authoredDropChance,
        bool sameMap)
    {
        if (!sameMap)
            return std::numeric_limits<float>::max() / 4.0f;

        float distance = std::sqrt(std::max(0.0f, distanceSq));
        float chance = authoredDropChance > 0.0f
            ? std::clamp(authoredDropChance, 0.1f, 100.0f)
            : 1.0f;
        float expectedAttempts = 100.0f / chance;
        return distance + expectedAttempts * 40.0f;
    }
}
