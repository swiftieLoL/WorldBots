#include "DruidStrategy.h"
#include "ClassStrategyUtils.h"
#include "Combat/CombatPositioning.h"

namespace Combat
{
    void DruidStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target)) return;
        _decisionTimer.Tick(deltaMs);
        if (bot->GetShapeshiftForm() != FORM_NONE)
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 5.0f, 28.0f);
        if (range != RangeAdjustment::Hold || bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        if (blackboard.self.healthPct < 30 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 22812, "Druid", "Barkskin"))
        { _decisionTimer.Set(1000); return; }
        if (blackboard.self.healthPct < 45 &&
            (ClassStrategyUtils::TryCastRank(bot, bot, 774, "Druid", "Rejuvenation") ||
             ClassStrategyUtils::TryCastRank(bot, bot, 5185, "Druid", "Healing Touch")))
        { if (movement) movement->Stop(); _decisionTimer.Set(1800); return; }
        if (!Helper::SpellUtils::HasAuraInChain(bot, 1126) &&
            ClassStrategyUtils::TryCastRank(bot, bot, 1126, "Druid", "Mark of the Wild"))
        { _decisionTimer.Set(1000); return; }
        if (bot->GetDistance(target) < 10.0f && !Helper::SpellUtils::HasAuraInChain(target, 339) &&
            ClassStrategyUtils::TryCastRank(bot, target, 339, "Druid", "Entangling Roots"))
        { _decisionTimer.Set(1500); return; }
        if (!Helper::SpellUtils::HasAuraInChain(target, 8921) &&
            ClassStrategyUtils::TryCastRank(bot, target, 8921, "Druid", "Moonfire"))
        { _decisionTimer.Set(1000); return; }
        if (!Helper::SpellUtils::HasAuraInChain(target, 5570) &&
            ClassStrategyUtils::TryCastRank(bot, target, 5570, "Druid", "Insect Swarm"))
        { _decisionTimer.Set(1000); return; }
        if (ClassStrategyUtils::TryCastRank(bot, target, 5176, "Druid", "Wrath"))
            _decisionTimer.Set(1800);
    }
}
