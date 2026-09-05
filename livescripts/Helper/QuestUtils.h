#pragma once

#include "Player.h"
#include "QuestDef.h"
#include "EquipmentUtils.h"

#include <cstdint>
#include <limits>

namespace Helper::QuestUtils
{
    // AddQuest() accepts the quest even when TrinityCore cannot place its
    // source item. Preflight that item so the bot never starts a delivery
    // quest it cannot physically carry.
    inline bool CanReceiveQuestSourceItem(Player* bot, Quest const* quest)
    {
        if (!bot || !quest)
            return false;

        uint32_t sourceItemId = quest->GetSrcItemId();
        if (!sourceItemId)
            return true;

        uint32_t sourceItemCount = quest->GetSrcItemCount();
        if (!sourceItemCount)
            sourceItemCount = 1;

        if (bot->GetItemCount(sourceItemId, false) >= sourceItemCount)
            return true;

        ItemPosCountVec destination;
        return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, sourceItemId, sourceItemCount) == EQUIP_ERR_OK;
    }

    // Returns the first required quest item that is missing from the bot's
    // inventory. Quest progress must use inventory-only counts here; bank,
    // mail, or other storage does not satisfy a turn-in.
    inline bool FindMissingRequiredItem(Player* bot, Quest const* quest,
                                        uint32_t& itemId, uint32_t& itemCount, uint32_t& requiredCount)
    {
        itemId = 0;
        itemCount = 0;
        requiredCount = 0;

        if (!bot || !quest)
            return false;

        for (uint32_t index = 0; index < quest->GetReqItemsCount(); ++index)
        {
            uint32_t requiredItemId = quest->RequiredItemId[index];
            uint32_t requiredItemCount = quest->RequiredItemCount[index];
            if (!requiredItemId || !requiredItemCount)
                continue;

            uint32_t currentCount = bot->GetItemCount(requiredItemId, false);
            if (currentCount < requiredItemCount)
            {
                itemId = requiredItemId;
                itemCount = currentCount;
                requiredCount = requiredItemCount;
                return true;
            }
        }

        return false;
    }

    // Checks both quest completion requirements and the inventory capacity for
    // the reward that will actually be selected. Choice rewards use the same
    // class/spec-aware equipment policy as auto-equip; a real upgrade wins,
    // then usable supplies/equipment, then vendor value.
    inline bool SelectRewardWithAvailableSpace(Player* bot, Quest const* quest, uint32_t& rewardIndex)
    {
        rewardIndex = 0;
        if (!bot || !quest || !bot->CanRewardQuest(quest, false))
            return false;

        uint32_t choiceCount = quest->GetRewChoiceItemsCount();
        if (choiceCount == 0)
            return bot->CanRewardQuest(quest, 0, false);

        bool hasChoiceItem = false;
        bool foundReward = false;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (uint32_t index = 0; index < choiceCount; ++index)
        {
            uint32_t itemId = quest->RewardChoiceItemId[index];
            if (!itemId)
                continue;

            hasChoiceItem = true;
            if (bot->CanRewardQuest(quest, index, false))
            {
                float score = Helper::EquipmentUtils::ScoreQuestReward(bot, itemId);
                if (!foundReward || score > bestScore)
                {
                    foundReward = true;
                    bestScore = score;
                    rewardIndex = index;
                }
            }
        }

        if (foundReward)
            return true;

        // Some templates expose a choice count but contain no item choices.
        return !hasChoiceItem && bot->CanRewardQuest(quest, 0, false);
    }

    // True only when the quest is otherwise rewardable, but none of its
    // reward selections fit in the current inventory. This deliberately does
    // not treat missing delivery items, money, or other prerequisites as a
    // vendoring problem.
    inline bool IsRewardBlockedByInventory(Player* bot, Quest const* quest)
    {
        if (!bot || !quest || !bot->CanRewardQuest(quest, false))
            return false;

        uint32_t rewardIndex = 0;
        return !SelectRewardWithAvailableSpace(bot, quest, rewardIndex);
    }
}
