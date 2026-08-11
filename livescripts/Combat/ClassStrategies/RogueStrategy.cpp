#include "RogueStrategy.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void RogueStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target)) return;
        _decisionTimer.Tick(deltaMs);
        if (!ClassStrategyUtils::MaintainMelee(bot, target, movement) ||
            bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady()) return;

        if (blackboard.self.healthPct < 35 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 5277, "Rogue", "Evasion"))
        { _decisionTimer.Set(1000); return; }

        if (target->IsNonMeleeSpellCast(false) &&
            ClassStrategyUtils::TryCastRank(bot, target, 1766, "Rogue", "Kick"))
        { _decisionTimer.Set(500); return; }

        uint8_t comboPoints = bot->GetComboPoints(target);
        if (comboPoints >= 3 &&
            ClassStrategyUtils::TryCastRank(bot, target, 2098, "Rogue", "Eviscerate"))
        { _decisionTimer.Set(700); return; }

        if (!Helper::SpellUtils::HasAuraInChain(target, 703) &&
            ClassStrategyUtils::TryCastRank(bot, target, 703, "Rogue", "Garrote"))
        { _decisionTimer.Set(700); return; }

        if (ClassStrategyUtils::TryCastRank(bot, target, 1752, "Rogue", "Sinister Strike"))
            _decisionTimer.Set(500);
    }
}
