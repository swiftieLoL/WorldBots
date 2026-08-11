#pragma once

#include <algorithm>
#include <cstdint>

namespace BotAuth
{
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
