#include "RogueStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void RogueStrategy::ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        if (!ClassStrategyUtils::MaintainMelee(bot, target, movement) ||
            bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady()) return;

        if (blackboard.self.healthPct < 35 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 5277, "Rogue", "Evasion"))
        { _decisionTimer.Set(1000); return; }

        if (ClassStrategyUtils::TryInterrupt(bot, target, 1766, "Rogue", "Kick", _decisionTimer, 500))
            return;

        uint8_t comboPoints = bot->GetComboPoints(target);
        if (comboPoints >= 4 &&
            ClassStrategyUtils::TryCastRank(bot, target, 2098, "Rogue", "Eviscerate"))
        { _decisionTimer.Set(700); return; }

        if (comboPoints >= 2 && !Helper::SpellUtils::HasAuraInChain(target, 1943) &&
            ClassStrategyUtils::TryCastRank(bot, target, 1943, "Rogue", "Rupture"))
        { _decisionTimer.Set(700); return; }

        if (ClassStrategyUtils::TryCastRank(bot, target, 1752, "Rogue", "Sinister Strike"))
            _decisionTimer.Set(500);
    }

    bool RogueStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        if (bot->IsWithinMeleeRange(threat) && ClassStrategyUtils::TryCastRank(bot, threat, 1776, GetName(), "Gouge"))
            return true;

        float dist = bot->GetDistance(threat);
        if (dist <= 10.0f && bot->HasSpell(2094) && ClassStrategyUtils::TryCast(bot, threat, 2094, GetName(), "Blind"))
            return true;

        if (bot->HasSpell(2983) && !bot->HasAura(2983) && ClassStrategyUtils::TryCast(bot, bot, 2983, GetName(), "Sprint"))
            return true;

        if (bot->HasSpell(5277) && !bot->HasAura(5277) && ClassStrategyUtils::TryCast(bot, bot, 5277, GetName(), "Evasion"))
            return true;

        return false;
    }
}
