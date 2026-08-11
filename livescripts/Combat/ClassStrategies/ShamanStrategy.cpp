#include "ShamanStrategy.h"
#include "ClassStrategyUtils.h"
#include "Combat/CombatPositioning.h"

namespace Combat
{
    void ShamanStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target)) return;
        _decisionTimer.Tick(deltaMs);
        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 5.0f, 28.0f);
        if (range != RangeAdjustment::Hold || bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        if (blackboard.self.healthPct < 40 &&
            (ClassStrategyUtils::TryCastRank(bot, bot, 8004, "Shaman", "Lesser Healing Wave") ||
             ClassStrategyUtils::TryCastRank(bot, bot, 331, "Shaman", "Healing Wave")))
        { if (movement) movement->Stop(); _decisionTimer.Set(1800); return; }
        if (!Helper::SpellUtils::HasAuraInChain(bot, 324) &&
            ClassStrategyUtils::TryCastRank(bot, bot, 324, "Shaman", "Lightning Shield"))
        { _decisionTimer.Set(1000); return; }
        if (target->IsNonMeleeSpellCast(false) &&
            ClassStrategyUtils::TryCastRank(bot, target, 8042, "Shaman", "Earth Shock"))
        { _decisionTimer.Set(700); return; }
        if (!Helper::SpellUtils::HasAuraInChain(target, 8050) &&
            ClassStrategyUtils::TryCastRank(bot, target, 8050, "Shaman", "Flame Shock"))
        { _decisionTimer.Set(1000); return; }
        if (ClassStrategyUtils::TryCastRank(bot, target, 403, "Shaman", "Lightning Bolt"))
            _decisionTimer.Set(1800);
    }
}
