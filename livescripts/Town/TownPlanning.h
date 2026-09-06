#pragma once

#include <cstdint>
#include <vector>

namespace Town
{
    enum class Service : uint8_t
    {
        Sell,
        Repair,
        Restock,
        CreateRewardSpace,
        PartyVisit,
        TurnInQuest
    };

    struct QuestTurnInCandidate
    {
        uint32_t questId = 0;
        bool rewardBlocked = false;
        bool hasKnownPosition = false;
        float distanceFromTownSq = 0.0f;
        // The periodic blackboard can briefly retain a completed quest after
        // RewardQuest has removed it from the live player state. Exclude that
        // stale row so TownRun cannot repeatedly report no-op successes.
        bool liveRewardable = true;
    };

    struct PlanningInput
    {
        uint32_t freeBagSlots = 0;
        uint32_t rewardReserveSlots = 5;
        bool hasSellableItems = false;
        bool needsInventoryCleanup = false;
        bool hasQuestItemCapacityBlock = false;
        bool needsRepair = false;
        bool needsRestock = false;
        bool hasInventoryVendor = false;
        bool hasRepairVendor = false;
        bool hasRestockVendor = false;
        bool partyNeedsVendor = false;
        uint64_t partyMemberGuid = 0;
        std::vector<QuestTurnInCandidate> completedQuests;
    };

    struct Step
    {
        Service service = Service::Sell;
        uint32_t questId = 0;

        bool operator==(const Step&) const = default;
    };

    struct Plan
    {
        std::vector<Step> steps;
        uint32_t targetFreeBagSlots = 0;
        uint64_t partyMemberGuid = 0;
        bool blockedByMissingVendor = false;
        bool blockedByProtectedInventory = false;

        bool Empty() const { return steps.empty(); }
        bool RequiresVendorVisit() const;
    };

    inline bool ShouldExecute(const Plan& plan)
    {
        return !plan.Empty() || plan.blockedByMissingVendor ||
            plan.blockedByProtectedInventory;
    }

    Plan BuildPlan(const PlanningInput& input);
    const char* DescribePrimaryPurpose(const Plan& plan);
}
