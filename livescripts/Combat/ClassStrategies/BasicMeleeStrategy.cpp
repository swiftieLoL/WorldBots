#include "ObjectAccessor.h"
#include "BasicMeleeStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void BasicMeleeStrategy::ExecuteCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        ClassStrategyUtils::MaintainMelee(bot, target, movement);
    }
}
