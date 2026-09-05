#pragma once

#include <cstdint>

namespace Combat
{
    inline constexpr uint32_t NonEngagementTimeoutMs = 15000;
    inline constexpr uint32_t NonExecutableTargetSuppressionSeconds = 120;

    inline bool ExceedsVoluntaryCombatRange(bool defendingSelf,
        float distance, float maxRange)
    {
        // The range ceiling prevents voluntary cross-country aggro. Once the
        // creature is already attacking this bot, combat must remain
        // executable so its class strategy can close range or flee.
        return !defendingSelf && maxRange > 0.0f && distance > maxRange;
    }

    constexpr bool HasEngagementProgress(bool /*botInCombat*/,
        bool targetEngagedWithBot, bool movementActive, bool hasPath,
        bool casting)
    {
        return targetEngagedWithBot || movementActive || hasPath || casting;
    }
}
