#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Brain
{
    inline uint32_t QuestLevelFitPenalty(uint32_t botLevel, int32_t questLevel)
    {
        // Non-positive quest levels scale with the player in TrinityCore.
        if (questLevel <= 0)
            return 0;

        int32_t delta = static_cast<int32_t>(botLevel) - questLevel;
        return static_cast<uint32_t>(delta < 0 ? -delta : delta);
    }

    template <typename ActiveQuest, typename IsEligible>
    bool HasActionableActiveQuest(const std::vector<ActiveQuest>& activeQuests,
        const std::unordered_set<uint32_t>& excludedQuestIds,
        IsEligible isEligible)
    {
        return std::any_of(activeQuests.begin(), activeQuests.end(),
            [&excludedQuestIds, &isEligible](const ActiveQuest& quest) {
                return quest.questId != 0 &&
                    excludedQuestIds.find(quest.questId) ==
                        excludedQuestIds.end() && isEligible(quest);
            });
    }

    template <typename ActiveQuest>
    bool HasActionableActiveQuest(const std::vector<ActiveQuest>& activeQuests,
        const std::unordered_set<uint32_t>& excludedQuestIds)
    {
        return HasActionableActiveQuest(activeQuests, excludedQuestIds,
            [](const ActiveQuest&) { return true; });
    }

    inline bool ShouldDiscoverWorldQuestStarter(
        bool hasActionableActiveQuest, bool hasNearbyAvailableQuest)
    {
        // Remote discovery is migration, not ordinary local shopping. Only do
        // it after the bot has exhausted actionable active and nearby work.
        return !hasActionableActiveQuest && !hasNearbyAvailableQuest;
    }
}
