#pragma once

#include "BotGoal.h"
#include "Helper/CommonTypes.h"
#include "Town/TownPlanning.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <unordered_map>
#include <variant>

namespace Brain
{
    struct TargetActionRequest
    {
        ObjectGuid targetGuid;
    };

    struct QuestActionRequest
    {
        uint32_t questId = 0;
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
    };

    struct UnstuckActionRequest
    {
        uint32_t deadlyQuestId = 0;
    };

    struct GrindActionRequest
    {
        int32_t minLevelOffset = -3;
        int32_t maxLevelOffset = 0;
    };

    struct TownRunActionRequest
    {
        Town::Plan plan;
    };

    using ActionPayload = std::variant<
        std::monostate,
        TargetActionRequest,
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
