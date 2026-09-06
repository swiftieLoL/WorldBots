#include "TownPlanning.h"

#include <algorithm>

namespace Town
{
    bool Plan::RequiresVendorVisit() const
    {
        return std::any_of(steps.begin(), steps.end(), [](const Step& step) {
            return step.service == Service::Sell ||
                   step.service == Service::Repair ||
                   step.service == Service::Restock ||
                   step.service == Service::CreateRewardSpace ||
                   step.service == Service::PartyVisit;
        });
    }

    Plan BuildPlan(const PlanningInput& input)
    {
        Plan plan;

        bool hasBlockedReward = std::any_of(input.completedQuests.begin(), input.completedQuests.end(),
            [](const QuestTurnInCandidate& quest) {
                return quest.liveRewardable &&
                    quest.hasKnownPosition && quest.rewardBlocked;
            });
        bool needsReservedSpace = hasBlockedReward && input.freeBagSlots < input.rewardReserveSlots;
        bool inventoryCleanupRequested = input.needsInventoryCleanup ||
            input.hasQuestItemCapacityBlock;
        bool shouldSell = input.hasSellableItems &&
            (inventoryCleanupRequested || needsReservedSpace);
        // A loot-capacity failure can be caused by poor unsellable items that
        // are safe to discard but are not reported as conventionally
        // sellable. Give VendorAction one bounded cleanup attempt instead of
        // returning an empty plan and trapping the TownRun goal on IdleAction.
        bool needsFallbackCapacityCleanup = inventoryCleanupRequested && !shouldSell;
        bool shouldCreateSpace = needsReservedSpace || needsFallbackCapacityCleanup;
        bool hasInventoryVendor = input.hasInventoryVendor;
        bool hasRepairVendor = input.hasRepairVendor;
        bool hasRestockVendor = input.hasRestockVendor;
        plan.targetFreeBagSlots = needsReservedSpace ? input.rewardReserveSlots :
            (needsFallbackCapacityCleanup ? 1u : 0u);

        bool shouldVisitForParty = input.partyNeedsVendor && input.hasInventoryVendor;
        bool needsInventoryVendor = shouldSell || shouldCreateSpace || shouldVisitForParty;
        if (shouldVisitForParty)
            plan.partyMemberGuid = input.partyMemberGuid;
        if ((needsInventoryVendor && !hasInventoryVendor) ||
            (input.needsRepair && !hasRepairVendor) ||
            (input.needsRestock && !hasRestockVendor))
            plan.blockedByMissingVendor = true;

        if (needsInventoryVendor && hasInventoryVendor)
        {
            if (shouldSell)
                plan.steps.push_back({ Service::Sell, 0 });

            if (shouldVisitForParty)
                plan.steps.push_back({ Service::PartyVisit, 0 });

            if (shouldCreateSpace)
            {
                plan.steps.push_back({ Service::CreateRewardSpace, 0 });
                if (needsReservedSpace && !shouldSell)
                    plan.blockedByProtectedInventory = true;
            }
        }

        // Repair is deliberately independent: towns commonly use a separate
        // repairer and supplier.
        if (input.needsRepair && hasRepairVendor)
            plan.steps.push_back({ Service::Repair, 0 });

        std::vector<QuestTurnInCandidate> turnIns;
        for (const QuestTurnInCandidate& quest : input.completedQuests)
        {
            if (quest.questId == 0 || !quest.liveRewardable || !quest.hasKnownPosition)
                continue;

            // Do not send the bot to a quest giver while its reward is known
            // to be blocked and there is no executable capacity step.
            if (quest.rewardBlocked && (plan.blockedByProtectedInventory ||
                !hasInventoryVendor || !needsInventoryVendor))
                continue;

            turnIns.push_back(quest);
        }

        std::stable_sort(turnIns.begin(), turnIns.end(),
            [](const QuestTurnInCandidate& left, const QuestTurnInCandidate& right) {
                return left.distanceFromTownSq < right.distanceFromTownSq;
            });

        for (const QuestTurnInCandidate& quest : turnIns)
            plan.steps.push_back({ Service::TurnInQuest, quest.questId });

        // Restock after reward collection so supplies cannot consume capacity
        // that the preceding phase deliberately reserved.
        if (input.needsRestock && hasRestockVendor)
            plan.steps.push_back({ Service::Restock, 0 });

        return plan;
    }

    const char* DescribePrimaryPurpose(const Plan& plan)
    {
        if (!plan.steps.empty())
        {
            switch (plan.steps.front().service)
            {
                case Service::Sell:
                case Service::CreateRewardSpace:
                    return "Inventory Cleanup";
                case Service::Repair:
                    return "Equipment Repair";
                case Service::Restock:
                    return "Supply Restock";
                case Service::PartyVisit:
                    return "Party Vendor Visit";
                case Service::TurnInQuest:
                    return "Quest Turn-In";
            }
        }

        if (plan.blockedByProtectedInventory)
            return "Inventory Capacity Blocked";
        if (plan.blockedByMissingVendor)
            return "Required Vendor Unavailable";
        return "Town Services";
    }
}
