#pragma once

#include "BotGoal.h"
#include "Brain/SuppressionRegistry.h"
#include "Helper/CommonTypes.h"
#include "Town/TownPlanning.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <variant>

namespace Brain
{
    struct TargetActionRequest
    {
        ObjectGuid targetGuid;
    };

    struct FollowActionRequest
    {
        ObjectGuid targetGuid;
        float distance = 2.0f;
        float angle = 0.0f;
    };

    struct QuestActionRequest
    {
        uint32_t questId = 0;
        std::vector<DangerArea> dangerAreas;
    };

    struct MoveActionRequest
    {
        Common::PositionInfo destination;
    };

    struct WanderActionRequest
    {
        Common::PositionInfo origin;
        float radius = 15.0f;
        std::unordered_map<uint32_t, uint32_t> suppressedQuests;
        std::vector<DestinationSuppression> suppressedDestinations;
    };

    struct UnstuckActionRequest
    {
        uint32_t deadlyQuestId = 0;
        bool progressionRecovery = false;
    };

    struct GrindActionRequest
    {
        int32_t minLevelOffset = -3;
        int32_t maxLevelOffset = 0;
        std::unordered_map<uint64_t, uint32_t> suppressedSpawnIds;
        std::unordered_map<uint32_t, uint32_t> suppressedCreatureEntries;
        std::vector<DestinationSuppression> suppressedDestinations;
        std::vector<DangerArea> dangerAreas;
    };

    struct TownRunActionRequest
    {
        Town::Plan plan;
        std::unordered_map<uint32_t, uint32_t> suppressedNpcEntries;
        float maxVendorTravelDistance = std::numeric_limits<float>::max();
        std::vector<DangerArea> dangerAreas;
    };

    using ActionPayload = std::variant<
        std::monostate,
        TargetActionRequest,
        FollowActionRequest,
        QuestActionRequest,
        MoveActionRequest,
        WanderActionRequest,
        GrindActionRequest,
        UnstuckActionRequest,
        TownRunActionRequest>;

    struct ActionRequest
    {
        BotGoal goal = BotGoal::Idle;
        ActionPayload payload;
    };
}
