#pragma once

#include <algorithm>
#include <cstdint>

namespace BotAuth
{
    inline bool ShouldPauseBotProvisioning(uint32_t startupGraceRemainingMs,
        uint32_t playerLoginGraceRemainingMs, uint32_t queuedPlayerCount,
        bool prioritizePlayerLogins)
    {
        return startupGraceRemainingMs != 0 ||
            (prioritizePlayerLogins &&
                (playerLoginGraceRemainingMs != 0 || queuedPlayerCount != 0));
    }

    inline bool CanLaunchBotLogin(uint32_t launchCooldownRemainingMs,
        uint32_t pendingLoginCount, uint32_t maxConcurrentLogins)
    {
        return launchCooldownRemainingMs == 0 && maxConcurrentLogins != 0 &&
            pendingLoginCount < maxConcurrentLogins;
    }

    inline uint32_t CalculateRetryDelayMs(uint32_t attempt, uint32_t initialDelayMs,
        uint32_t maximumDelayMs)
    {
        if (initialDelayMs == 0 || maximumDelayMs == 0)
            return 0;

        uint64_t delay = initialDelayMs;
        for (uint32_t i = 0; i < attempt && delay < maximumDelayMs; ++i)
            delay = std::min<uint64_t>(delay * 2, maximumDelayMs);
        return static_cast<uint32_t>(std::min<uint64_t>(delay, maximumDelayMs));
    }
}
