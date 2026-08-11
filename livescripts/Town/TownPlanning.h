#pragma once

#include <cstdint>
#include <vector>

namespace Town
{
    enum class Service : uint8_t
    {
        Sell,
        Repair,
        CreateRewardSpace,
        TurnInQuest
    };

    struct QuestTurnInCandidate
    {
        uint32_t questId = 0;
        bool rewardBlocked = false;
        bool onCurrentMap = false;
        bool hasKnownPosition = false;
        float distanceFromTownSq = 0.0f;
    };

    struct PlanningInput
    {
        uint32_t freeBagSlots = 0;
        uint32_t rewardReserveSlots = 5;
        bool hasSellableItems = false;
        bool needsInventoryCleanup = false;
        bool needsRepair = false;
        bool hasUsableVendor = false;
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
        bool blockedByMissingVendor = false;
        bool blockedByProtectedInventory = false;

        bool Empty() const { return steps.empty(); }
        bool RequiresVendorVisit() const;
    };

    Plan BuildPlan(const PlanningInput& input);

    enum class VerificationResult : uint8_t
    {
        Succeeded,
        RetryableFailure,
        Blocked
    };

    VerificationResult VerifyInventoryService(
        Service service,
        uint32_t freeSlotsBefore,
        uint32_t freeSlotsAfter,
        uint8_t durabilityBefore,
        uint8_t durabilityAfter,
        bool stillNeedsRepair);
}
