#include "DeathKnightStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void DeathKnightStrategy::ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        bool inMelee = bot->IsWithinMeleeRange(target);

        if (!inMelee && bot->GetDistance(target) <= 30.0f && _decisionTimer.IsReady() &&
            ClassStrategyUtils::TryCastRank(bot, target, 49576, "Death Knight", "Death Grip"))
        { _decisionTimer.Set(1000); return; }

        inMelee = ClassStrategyUtils::MaintainMelee(bot, target, movement);
        if (bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady()) return;

        if (ClassStrategyUtils::TryMaintainAura(bot, bot, 57330, "Death Knight", "Horn of Winter", _decisionTimer, 1000))
            return;
        if (blackboard.self.healthPct < 25 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 48792, "Death Knight", "Icebound Fortitude"))
        { _decisionTimer.Set(1000); return; }
        if (ClassStrategyUtils::TryInterrupt(bot, target, 47528, "Death Knight", "Mind Freeze", _decisionTimer, 500))
            return;
        if (!inMelee) return;

        if (blackboard.self.healthPct < 60 &&
            ClassStrategyUtils::TryCastRank(bot, target, 49998, "Death Knight", "Death Strike"))
        { _decisionTimer.Set(800); return; }

        if (!target->HasAura(55095, bot->GetGUID()) &&
            ClassStrategyUtils::TryCastRank(bot, target, 45477, "Death Knight", "Icy Touch"))
        { _decisionTimer.Set(700); return; }
        if (!target->HasAura(55078, bot->GetGUID()) &&
            ClassStrategyUtils::TryCastRank(bot, target, 45462, "Death Knight", "Plague Strike"))
        { _decisionTimer.Set(700); return; }

        static constexpr ClassStrategyUtils::PrioritySpell prioritySpells[] = {
            { 45902, "Blood Strike", 700 },
            { 47541, "Death Coil", 700 }
        };
        ClassStrategyUtils::TryCastPriorityList(bot, target, prioritySpells, "Death Knight", _decisionTimer);
    }

    bool DeathKnightStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);

        if (dist <= 30.0f && ClassStrategyUtils::TryCastRank(bot, threat, 45524, GetName(), "Chains of Ice"))
            return true;

        return false;
    }
}
