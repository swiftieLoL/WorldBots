#pragma once

#include "BotBlackboard.h"

class Player;
class Quest;

namespace Blackboard
{
    class QuestItemSourceResolver
    {
    public:
        static QuestObjectiveData Resolve(Player* bot, Quest const* questTemplate, QuestState& questState,
            uint32_t questId, uint32_t itemId, uint32_t requiredCount, bool forceFullRescan);
    };
}
