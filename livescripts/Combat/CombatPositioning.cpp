#include "Globals/ObjectMgr.h"
#include "CombatPositioning.h"
#include "Helper/MovementManager.h"
#include "Player.h"
#include "Unit.h"
#include <cmath>

namespace Combat
{
    bool CombatPositioning::MaintainRanged(Player* bot, Unit* target, MovementManager* movement, float maximumRange)
    {
        if (!bot || !target)
            return false;

        bool hasLineOfSight = bot->IsWithinLOSInMap(target);
        if (bot->GetDistance(target) > maximumRange || !hasLineOfSight)
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

        return hasLineOfSight;
    }

    RangeAdjustment CombatPositioning::MaintainRangeBand(Player* bot, Unit* target,
        MovementManager* movement, float minimumRange, float maximumRange)
    {
        if (!bot || !target)
            return RangeAdjustment::CloseDistance;

        bool hasLineOfSight = bot->IsWithinLOSInMap(target);
        float distance = bot->GetDistance(target);
        RangeAdjustment adjustment = ChooseRangeAdjustment(
            distance, minimumRange, maximumRange, hasLineOfSight);

        if (adjustment == RangeAdjustment::CloseDistance)
        {
            if (movement)
                movement->MoveTo(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                    BotMovementState::Moving, false);
        }
        else if (adjustment == RangeAdjustment::CreateDistance)
        {
            float dx = bot->GetPositionX() - target->GetPositionX();
            float dy = bot->GetPositionY() - target->GetPositionY();
            float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.1f)
            {
                dx = std::cos(bot->GetOrientation());
                dy = std::sin(bot->GetOrientation());
                length = 1.0f;
            }

            float retreatDistance = (minimumRange - distance) + 4.0f;
            if (movement)
                movement->MoveTo(bot->GetPositionX() + dx / length * retreatDistance,
                    bot->GetPositionY() + dy / length * retreatDistance,
                    bot->GetPositionZ(), BotMovementState::Fleeing, false);
        }
        else
        {
            if (movement)
                movement->Stop();
            bot->SetInFront(target);
        }

        return adjustment;
    }
}
