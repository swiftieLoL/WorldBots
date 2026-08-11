#include "PaladinStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void PaladinStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target)) return;
        _decisionTimer.Tick(deltaMs);
        bool inMelee = ClassStrategyUtils::MaintainMelee(bot, target, movement);
        if (bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady()) return;

        if (blackboard.self.healthPct < 25 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 498, "Paladin", "Divine Protection"))
        { _decisionTimer.Set(1000); return; }
        if (blackboard.self.healthPct < 40 &&
            (ClassStrategyUtils::TryCastRank(bot, bot, 19750, "Paladin", "Flash of Light") ||
             ClassStrategyUtils::TryCastRank(bot, bot, 635, "Paladin", "Holy Light")))
        { if (movement) movement->Stop(); _decisionTimer.Set(1500); return; }

        if (!Helper::SpellUtils::HasAuraInChain(bot, 21084) &&
            !Helper::SpellUtils::HasAuraInChain(bot, 20375) &&
            (ClassStrategyUtils::TryCastRank(bot, bot, 20375, "Paladin", "Seal of Command") ||
             ClassStrategyUtils::TryCastRank(bot, bot, 21084, "Paladin", "Seal of Righteousness")))
        { _decisionTimer.Set(1000); return; }

        if (!inMelee) return;
        if (target->HealthBelowPct(20) &&
            ClassStrategyUtils::TryCastRank(bot, target, 24275, "Paladin", "Hammer of Wrath"))
        { _decisionTimer.Set(1000); return; }

        static constexpr uint32_t priority[] = { 53385, 35395, 20271 };
        static constexpr const char* names[] = { "Divine Storm", "Crusader Strike", "Judgement" };
        for (size_t i = 0; i < std::size(priority); ++i)
            if (ClassStrategyUtils::TryCastRank(bot, target, priority[i], "Paladin", names[i]))
            { _decisionTimer.Set(1000); return; }
    }
}
