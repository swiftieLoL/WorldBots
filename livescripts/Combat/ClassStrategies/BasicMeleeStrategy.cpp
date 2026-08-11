#include "Globals/ObjectMgr.h"
#include "BasicMeleeStrategy.h"
#include "ObjectAccessor.h"

namespace Combat
{
    void BasicMeleeStrategy::UpdateCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/,
        uint32_t /*deltaMs*/)
    {
        if (!bot || !bot->IsInWorld() || !target || !target->IsInWorld() || !target->IsAlive())
            return;

        ObjectGuid targetGuid = target->GetGUID();
        if (bot->GetVictim() != target)
            bot->Attack(target, true);

        target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive())
            return;

        if (!bot->IsWithinMeleeRange(target))
        {
            if (movement)
                movement->MoveTo(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                    BotMovementState::Moving, false);
        }
        else
        {
            if (movement)
                movement->Stop();
            bot->SetInFront(target);
        }
    }
}
