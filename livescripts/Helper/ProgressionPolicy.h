#pragma once

#include <algorithm>
#include <cstdint>

namespace Helper
{
    struct GrindingLevelBand
    {
        int32_t minOffset = -3;
        int32_t maxOffset = 0;
    };

    inline GrindingLevelBand SelectGrindingLevelBand(int32_t configuredMinOffset,
        int32_t configuredMaxOffset, bool conservative)
    {
        if (!conservative)
            return { configuredMinOffset, configuredMaxOffset };

        return {
            std::min<int32_t>(configuredMinOffset, -5),
            std::min<int32_t>(configuredMaxOffset, -2)
        };
    }

    inline bool IsQuestLevelSuitable(uint32_t botLevel, int32_t questLevel, int32_t maxLevelsAboveBot)
    {
        // TrinityCore uses a non-positive quest level for quests that scale to
        // the player. Those are suitable by definition.
        if (questLevel <= 0)
            return true;

        int32_t safeAllowance = std::max<int32_t>(0, maxLevelsAboveBot);
        return questLevel <= static_cast<int32_t>(botLevel) + safeAllowance;
    }

    inline bool IsQuestGroupStructureSuitable(bool inGroup, uint32_t questType, uint32_t suggestedPlayers)
    {
        if (inGroup)
            return true;

        // In TrinityCore: 81=Dungeon, 62=Raid, 88=Raid10, 89=Raid25
        if (questType == 81 || questType == 62 || questType == 88 || questType == 89)
            return false;

        if (suggestedPlayers > 1)
            return false;

        return true;
    }

    inline bool IsGrindingLevelSuitable(uint32_t botLevel, uint32_t creatureLevel,
        int32_t minLevelOffset, int32_t maxLevelOffset)
    {
        int32_t level = static_cast<int32_t>(creatureLevel);
        int32_t low = std::max<int32_t>(1, static_cast<int32_t>(botLevel) + minLevelOffset);
        int32_t high = std::max<int32_t>(low, static_cast<int32_t>(botLevel) + maxLevelOffset);
        return level >= low && level <= high;
    }

    inline bool IsCreatureAboveGrindingCeiling(uint32_t botLevel,
        uint32_t creatureLevel, int32_t maxLevelOffset)
    {
        int32_t high = std::max<int32_t>(1,
            static_cast<int32_t>(botLevel) + maxLevelOffset);
        return static_cast<int32_t>(creatureLevel) > high;
    }

    inline bool IsQuestObjectiveCreatureSuitable(uint32_t botLevel,
        uint32_t creatureMaximumLevel, int32_t maxLevelsAboveBot)
    {
        int32_t allowance = std::max<int32_t>(0, maxLevelsAboveBot);
        return static_cast<int32_t>(creatureMaximumLevel) <=
            static_cast<int32_t>(botLevel) + allowance;
    }

    inline uint32_t NextProgressionRetryLevel(uint32_t botLevel)
    {
        return botLevel < 255 ? botLevel + 1 : botLevel;
    }
}
