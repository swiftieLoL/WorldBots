#include "TownPlanning.h"

#include <algorithm>

namespace Town
{
    bool Plan::RequiresVendorVisit() const
    {
        return std::any_of(steps.begin(), steps.end(), [](const Step& step) {
            return step.service == Service::Sell ||
                   step.service == Service::Repair ||
                   step.service == Service::CreateRewardSpace;
        });
    }

    Plan BuildPlan(const PlanningInput& input)
    {
        Plan plan;

        bool hasBlockedReward = std::any_of(input.completedQuests.begin(), input.completedQuests.end(),
            [](const QuestTurnInCandidate& quest) {
                return quest.onCurrentMap && quest.hasKnownPosition && quest.rewardBlocked;
            });
        bool needsReservedSpace = hasBlockedReward && input.freeBagSlots < input.rewardReserveSlots;
        bool shouldSell = input.hasSellableItems && (input.needsInventoryCleanup || needsReservedSpace);
        // A loot-capacity failure can be caused by poor unsellable items that
        // are safe to discard but are not reported as conventionally
        // sellable. Give VendorAction one bounded cleanup attempt instead of
        // returning an empty plan and trapping the TownRun goal on IdleAction.
        bool needsFallbackCapacityCleanup = input.needsInventoryCleanup && !shouldSell;
        bool shouldCreateSpace = needsReservedSpace || needsFallbackCapacityCleanup;
        bool needsVendor = shouldSell || shouldCreateSpace || input.needsRepair;
        plan.targetFreeBagSlots = needsReservedSpace ? input.rewardReserveSlots :
            (needsFallbackCapacityCleanup ? 1u : 0u);

        if (needsVendor && !input.hasUsableVendor)
        {
            plan.blockedByMissingVendor = true;
        }
        else if (needsVendor)
        {
            // A single live vendor visit performs these operations in this
            // order. Keeping the individual steps makes the contract clear
            // and leaves room for service-specific NPCs in a later planner.
            if (shouldSell)
                plan.steps.push_back({ Service::Sell, 0 });

            if (shouldCreateSpace)
            {
                plan.steps.push_back({ Service::CreateRewardSpace, 0 });
                if (needsReservedSpace && !shouldSell)
                    plan.blockedByProtectedInventory = true;
            }

            if (input.needsRepair)
                plan.steps.push_back({ Service::Repair, 0 });
        }

        std::vector<QuestTurnInCandidate> turnIns;
        for (const QuestTurnInCandidate& quest : input.completedQuests)
        {
            if (quest.questId == 0 || !quest.onCurrentMap || !quest.hasKnownPosition)
                continue;

            // Do not send the bot to a quest giver while its reward is known
            // to be blocked and there is no executable capacity step.
            if (quest.rewardBlocked && (!input.hasUsableVendor || !shouldSell))
                continue;

            turnIns.push_back(quest);
        }

        std::stable_sort(turnIns.begin(), turnIns.end(),
            [](const QuestTurnInCandidate& left, const QuestTurnInCandidate& right) {
                return left.distanceFromTownSq < right.distanceFromTownSq;
            });

        for (const QuestTurnInCandidate& quest : turnIns)
            plan.steps.push_back({ Service::TurnInQuest, quest.questId });

        return plan;
    }

    VerificationResult VerifyInventoryService(
        Service service,
        uint32_t freeSlotsBefore,
        uint32_t freeSlotsAfter,
        uint8_t durabilityBefore,
        uint8_t durabilityAfter,
        bool stillNeedsRepair)
    {
        switch (service)
        {
            case Service::Sell:
            case Service::CreateRewardSpace:
                return freeSlotsAfter > freeSlotsBefore
                    ? VerificationResult::Succeeded
                    : VerificationResult::RetryableFailure;

            case Service::Repair:
                if (!stillNeedsRepair && durabilityAfter >= durabilityBefore)
                    return VerificationResult::Succeeded;
                return durabilityAfter > durabilityBefore
                    ? VerificationResult::RetryableFailure
                    : VerificationResult::Blocked;

            case Service::TurnInQuest:
            default:
                return VerificationResult::Blocked;
        }
    }
}
