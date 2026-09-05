#pragma once

#include "Blackboard/BotBlackboard.h"

class Player;
class Quest;

namespace Sense
{
    class QuestItemSourceResolver
    {
    public:
        static Blackboard::QuestObjectiveData Resolve(Player* bot, Quest const* questTemplate, Blackboard::QuestState& questState,
            uint32_t questId, uint32_t itemId, uint32_t requiredCount, bool forceFullRescan);
    };
}
