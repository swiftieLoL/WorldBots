#pragma once

#include "Blackboard/BotBlackboard.h"
#include <cstdint>

class Player;
class Quest;

namespace Sense
{
    class QuestTargetResolver
    {
    public:
        static uint64_t ItemSourceCacheKey(uint32_t questId, uint32_t itemId);
        static bool ResolveExplorationTarget(Player* bot, Quest const* quest, Blackboard::PositionInfo& position, float& radius);
        static bool ResolveNearestQuestEnder(Player* bot, uint32_t questId, uint32_t& entry,
            Blackboard::QuestTargetKind& kind, Blackboard::PositionInfo& position);

    private:
        static float ResolveExplorationHeight(Player* bot, uint32_t mapId, float x, float y, float fallbackZ);
    };
}
