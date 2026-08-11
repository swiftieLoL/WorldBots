#include "WarlockStrategy.h"
#include "ClassStrategyUtils.h"
#include "Combat/CombatPositioning.h"

namespace Combat
{
    void WarlockStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target)) return;
        _decisionTimer.Tick(deltaMs);
        ClassStrategyUtils::EngagePet(bot, target);

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 6.0f, 28.0f);
        if (range != RangeAdjustment::Hold || bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        if (!bot->GetPet() &&
            ClassStrategyUtils::TryCastRank(bot, bot, 688, "Warlock", "Summon Imp"))
        { _decisionTimer.Set(6000); return; }
        if (blackboard.self.healthPct < 30 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 6229, "Warlock", "Shadow Ward"))
        { _decisionTimer.Set(1000); return; }
        if (blackboard.self.manaPct < 20 && blackboard.self.healthPct > 55 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 1454, "Warlock", "Life Tap"))
        { _decisionTimer.Set(1000); return; }
        if (!Helper::SpellUtils::HasAuraInChain(bot, 687) &&
            ClassStrategyUtils::TryCastRank(bot, bot, 687, "Warlock", "Demon Armor"))
        { _decisionTimer.Set(1000); return; }
        if (bot->GetDistance(target) < 9.0f && !Helper::SpellUtils::HasAuraInChain(target, 5782) &&
            ClassStrategyUtils::TryCastRank(bot, target, 5782, "Warlock", "Fear"))
        { _decisionTimer.Set(1500); return; }
        if (!Helper::SpellUtils::HasAuraInChain(target, 172) &&
            ClassStrategyUtils::TryCastRank(bot, target, 172, "Warlock", "Corruption"))
        { _decisionTimer.Set(1000); return; }
        if (!Helper::SpellUtils::HasAuraInChain(target, 980) &&
            ClassStrategyUtils::TryCastRank(bot, target, 980, "Warlock", "Curse of Agony"))
        { _decisionTimer.Set(1000); return; }
        if (!Helper::SpellUtils::HasAuraInChain(target, 348) &&
            ClassStrategyUtils::TryCastRank(bot, target, 348, "Warlock", "Immolate"))
        { _decisionTimer.Set(1500); return; }
        if (blackboard.self.healthPct < 60 &&
            ClassStrategyUtils::TryCastRank(bot, target, 689, "Warlock", "Drain Life"))
        { _decisionTimer.Set(3000); return; }
        if (ClassStrategyUtils::TryCastRank(bot, target, 686, "Warlock", "Shadow Bolt"))
            _decisionTimer.Set(2000);
    }
}
