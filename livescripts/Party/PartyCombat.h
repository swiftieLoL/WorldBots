#pragma once

class MovementManager;
class Player;
class Unit;

namespace Blackboard
{
    struct BotBlackboard;
}

namespace Party
{
    // Returns true when a role-specific heal, taunt, or repositioning command
    // consumed this combat update.
    bool HandleRoleAction(Player* bot, Unit* hostileTarget, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard);
}
