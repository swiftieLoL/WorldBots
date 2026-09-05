#include "MageStrategy.h"
#include "Combat/CombatPositioning.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void MageStrategy::ExecuteCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 0.0f, 28.0f);

        if (range != RangeAdjustment::Hold)
            return;

        // Defensive Frost Nova if target gets closer than 8 yards
        float dist = bot->GetDistance(target);
        if (dist <= 8.0f && _decisionTimer.IsReady())
        {
            if (ClassStrategyUtils::TryCastRank(bot, target, 122, GetName(), "Frost Nova"))
            {
                _decisionTimer.Set(1500);
                return;
            }
        }

        if (bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        static const ClassStrategyUtils::PrioritySpell dpsSpells[] = {
            { 116, "Frostbolt", 1500 },
            { 133, "Fireball", 1500 },
        };

        if (ClassStrategyUtils::TryCastPriorityList(bot, target, dpsSpells, GetName(), _decisionTimer))
            return;
    }

    bool MageStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);
        if (dist <= 10.0f && ClassStrategyUtils::TryCastRank(bot, threat, 122, GetName(), "Frost Nova"))
            return true;

        if (dist <= 8.0f && bot->HasSpell(1953) && ClassStrategyUtils::TryCast(bot, bot, 1953, GetName(), "Blink"))
            return true;

        return false;
    }
}
