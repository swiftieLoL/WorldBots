#include "DeathKnightStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void DeathKnightStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target)) return;
        _decisionTimer.Tick(deltaMs);
        bool inMelee = bot->IsWithinMeleeRange(target);

        if (!inMelee && bot->GetDistance(target) <= 30.0f && _decisionTimer.IsReady() &&
            ClassStrategyUtils::TryCastRank(bot, target, 49576, "Death Knight", "Death Grip"))
        { _decisionTimer.Set(1000); return; }

        inMelee = ClassStrategyUtils::MaintainMelee(bot, target, movement);
        if (bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady()) return;

        if (!Helper::SpellUtils::HasAuraInChain(bot, 57330) &&
            ClassStrategyUtils::TryCastRank(bot, bot, 57330, "Death Knight", "Horn of Winter"))
        { _decisionTimer.Set(1000); return; }
        if (blackboard.self.healthPct < 25 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 48792, "Death Knight", "Icebound Fortitude"))
        { _decisionTimer.Set(1000); return; }
        if (target->IsNonMeleeSpellCast(false) &&
            ClassStrategyUtils::TryCastRank(bot, target, 47528, "Death Knight", "Mind Freeze"))
        { _decisionTimer.Set(500); return; }
        if (!inMelee) return;

        if (blackboard.self.healthPct < 60 &&
            ClassStrategyUtils::TryCastRank(bot, target, 49998, "Death Knight", "Death Strike"))
        { _decisionTimer.Set(800); return; }

        static constexpr uint32_t priority[] = { 45477, 45462, 45902, 47541 };
        static constexpr const char* names[] = { "Icy Touch", "Plague Strike", "Blood Strike", "Death Coil" };
        for (size_t i = 0; i < std::size(priority); ++i)
            if (ClassStrategyUtils::TryCastRank(bot, target, priority[i], "Death Knight", names[i]))
            { _decisionTimer.Set(700); return; }
    }
}
