#pragma once

#include <cstdint>

namespace Actions::TurnInInteractionPolicy
{
    inline constexpr uint32_t QuestGiverResolveTimeoutMs = 5000;
    inline constexpr float LiveQuestEnderResolveRange = 100.0f;

    constexpr bool HasResolveTimedOut(uint32_t elapsedMs)
    {
        return elapsedMs >= QuestGiverResolveTimeoutMs;
    }

    constexpr bool NeedsWorldTravel(bool sameMap, float distance)
    {
        return !sameMap || distance > LiveQuestEnderResolveRange;
    }
}
