#include "WarriorStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void WarriorStrategy::ExecuteCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        if (bot->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_CHARGING) || bot->IsNonMeleeSpellCast(false))
            return;

        // 0. Stance Check: Ensure Warrior is in Battle Stance (or Defensive/Berserker if active)
        if (bot->HasSpell(2457) && !bot->HasAura(2457) && !bot->HasAura(71) && !bot->HasAura(2458))
        {
            if (ClassStrategyUtils::TryCast(bot, bot, 2457, GetName(), "Battle Stance"))
                return;
        }

        float dist = bot->GetDistance(target);
        bool hasLineOfSight = bot->IsWithinLOSInMap(target);

        // 1. Charge opener if between 8 and 25 yards (Check Charge BEFORE issuing normal MoveTo)
        if (hasLineOfSight && dist >= 8.0f && dist <= 25.0f && _decisionTimer.IsReady())
        {
            if (ClassStrategyUtils::TryCastRank(bot, target, 100, GetName(), "Charge"))
            {
                _decisionTimer.Set(1500);
                return;
            }
        }

        // Positioning: Move to true melee range
        if (!ClassStrategyUtils::MaintainMelee(bot, target, movement))
            return;

        if (!_decisionTimer.IsReady()) return;

        // 1. Rend if not already active on target
        if (ClassStrategyUtils::TryMaintainAura(bot, target, 772, GetName(), "Rend", _decisionTimer, 1500))
            return;

        // 2. Victory Rush if proc buff is active (Spell 34428 / Proc 32216)
        if (bot->HasSpell(34428) && (bot->HasAura(34428) || bot->HasAura(32216)))
        {
            if (ClassStrategyUtils::TryCast(bot, target, 34428, GetName(), "Victory Rush"))
            {
                _decisionTimer.Set(1500);
                return;
            }
        }

        // 3. Heroic Strike
        if (ClassStrategyUtils::TryCastRank(bot, target, 78, GetName(), "Heroic Strike"))
        {
            _decisionTimer.Set(1500);
            return;
        }
    }

    bool WarriorStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);
        if (dist <= 8.0f && ClassStrategyUtils::TryCastRank(bot, threat, 5246, GetName(), "Intimidating Shout"))
            return true;

        if (bot->IsWithinMeleeRange(threat) && ClassStrategyUtils::TryCastRank(bot, threat, 1715, GetName(), "Hamstring"))
            return true;

        return false;
    }
}
