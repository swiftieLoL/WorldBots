#pragma once

#include <cstdint>

namespace Brain
{
    enum class BotGoal : uint8_t
    {
        Idle = 0,
        Wander = 1,
        MoveToNpc = 2,
        FollowTarget = 3,
        Combat = 4,
        Flee = 5,
        AcceptQuest = 6,
        TurnInQuest = 7,
        ProgressQuest = 8,
        Loot = 9,
        Vendor = 10,
        Rest = 11,
        Resurrect = 12,
        Unstuck = 13,
        TownRun = 14,
        Grind = 15
    };
}
