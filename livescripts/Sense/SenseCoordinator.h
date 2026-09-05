#pragma once

#include "Blackboard/BotBlackboard.h"
#include <cstdint>

class MovementManager;
class Player;

namespace Sense
{
    class SenseCoordinator
    {
    public:
        static void ClearSharedCaches();
        static void UpdateAll(Player* bot, MovementManager* movement, Blackboard::BotBlackboard& bb, uint32_t deltaMs);
    };
}
