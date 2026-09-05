#include "CombatPositioning.h"
#include "Helper/MovementManager.h"
#include "Player.h"
#include "Unit.h"
#include <cmath>

namespace Combat
{
    namespace
    {
        bool HasActiveCast(Player* bot)
        {
            return bot && (bot->HasUnitState(UNIT_STATE_CASTING) ||
                bot->IsNonMeleeSpellCast(false));
        }
    }

    RangeAdjustment CombatPositioning::MaintainRangeBand(Player* bot, Unit* target,
        MovementManager* movement, float minimumRange, float maximumRange)
    {
        if (!bot || !target)
            return RangeAdjustment::CloseDistance;

        bool hasLineOfSight = bot->IsWithinLOSInMap(target);
        float distance = bot->GetDistance(target);
        bool preserveActiveCast = HasActiveCast(bot);
        RangeAdjustment adjustment = ChooseRangeAdjustment(
            distance, minimumRange, maximumRange, hasLineOfSight, preserveActiveCast);

        if (preserveActiveCast)
            return adjustment;

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
                // Orientation faces toward target; add PI to retreat away
                float awayAngle = bot->GetOrientation() + static_cast<float>(M_PI);
                dx = std::cos(awayAngle);
                dy = std::sin(awayAngle);
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
