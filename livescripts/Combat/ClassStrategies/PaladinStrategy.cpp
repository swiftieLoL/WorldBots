#include "PaladinStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void PaladinStrategy::ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        if (bot->IsNonMeleeSpellCast(false))
            return;

        bool inMelee = ClassStrategyUtils::MaintainMelee(bot, target, movement);
        if (!_decisionTimer.IsReady())
            return;

        if (blackboard.self.healthPct < 25 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 498, "Paladin", "Divine Protection"))
        { _decisionTimer.Set(1000); return; }
        if (blackboard.self.healthPct < 40)
        {
            if (movement) movement->Stop();
            if (ClassStrategyUtils::TryCastRank(bot, bot, 19750, "Paladin", "Flash of Light") ||
                ClassStrategyUtils::TryCastRank(bot, bot, 635, "Paladin", "Holy Light"))
            { _decisionTimer.Set(1500); return; }
        }

        if (!Helper::SpellUtils::HasAuraInChain(bot, 21084) &&
            !Helper::SpellUtils::HasAuraInChain(bot, 20375) &&
            (ClassStrategyUtils::TryCastRank(bot, bot, 20375, "Paladin", "Seal of Command") ||
             ClassStrategyUtils::TryCastRank(bot, bot, 21084, "Paladin", "Seal of Righteousness")))
        { _decisionTimer.Set(1000); return; }

        if (!inMelee) return;
        if (target->HealthBelowPct(20) &&
            ClassStrategyUtils::TryCastRank(bot, target, 24275, "Paladin", "Hammer of Wrath"))
        { _decisionTimer.Set(1000); return; }

        static constexpr ClassStrategyUtils::PrioritySpell prioritySpells[] = {
            { 53385, "Divine Storm", 1000 },
            { 35395, "Crusader Strike", 1000 },
            { 20271, "Judgement", 1000 }
        };
        ClassStrategyUtils::TryCastPriorityList(bot, target, prioritySpells, "Paladin", _decisionTimer);
    }

    bool PaladinStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);

        if (dist <= 10.0f && ClassStrategyUtils::TryCastRank(bot, threat, 853, GetName(), "Hammer of Justice"))
            return true;

        if (!bot->HasAura(25771)) // Forbearance
        {
            if (bot->HasSpell(642) && ClassStrategyUtils::TryCast(bot, bot, 642, GetName(), "Divine Shield"))
                return true;
            if (bot->HasSpell(498) && ClassStrategyUtils::TryCast(bot, bot, 498, GetName(), "Divine Protection"))
                return true;
        }

        return false;
    }
}
