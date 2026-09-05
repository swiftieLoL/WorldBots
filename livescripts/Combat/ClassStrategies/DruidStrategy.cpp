#include "DruidStrategy.h"
#include "ClassStrategyUtils.h"
#include "Combat/CombatPositioning.h"

namespace Combat
{
    void DruidStrategy::ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        if (blackboard.party.role == Party::Role::Tank)
        {
            if (bot->GetShapeshiftForm() != FORM_BEAR && bot->GetShapeshiftForm() != FORM_DIREBEAR)
            {
                if (_decisionTimer.IsReady() &&
                    ClassStrategyUtils::TryCastRank(bot, bot, 5487, "Druid", "Bear Form"))
                    _decisionTimer.Set(1000);
                return;
            }
            bool inMelee = ClassStrategyUtils::MaintainMelee(bot, target, movement);
            if (!inMelee || bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
                return;
            if (blackboard.self.healthPct < 35 &&
                ClassStrategyUtils::TryCastRank(bot, bot, 22812, "Druid", "Barkskin"))
            { _decisionTimer.Set(1000); return; }
            if (ClassStrategyUtils::TryCastRank(bot, target, 6807, "Druid", "Maul"))
                _decisionTimer.Set(1000);
            return;
        }
        if (bot->GetShapeshiftForm() != FORM_NONE)
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 0.0f, 28.0f);
        if (range != RangeAdjustment::Hold || bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        if (blackboard.self.healthPct < 30 &&
            ClassStrategyUtils::TryCastRank(bot, bot, 22812, "Druid", "Barkskin"))
        { _decisionTimer.Set(1000); return; }
        if (blackboard.self.healthPct < 45)
        {
            if (movement) movement->Stop();
            if (ClassStrategyUtils::TryCastRank(bot, bot, 774, "Druid", "Rejuvenation") ||
                ClassStrategyUtils::TryCastRank(bot, bot, 5185, "Druid", "Healing Touch"))
            { _decisionTimer.Set(1800); return; }
        }
        if (ClassStrategyUtils::TryMaintainAura(bot, bot, 1126, "Druid", "Mark of the Wild", _decisionTimer, 1000))
            return;
        if (bot->GetDistance(target) < 10.0f &&
            ClassStrategyUtils::TryMaintainAura(bot, target, 339, "Druid", "Entangling Roots", _decisionTimer, 1500))
            return;
        if (ClassStrategyUtils::TryMaintainAura(bot, target, 8921, "Druid", "Moonfire", _decisionTimer, 1000))
            return;
        if (ClassStrategyUtils::TryMaintainAura(bot, target, 5570, "Druid", "Insect Swarm", _decisionTimer, 1000))
            return;
        if (ClassStrategyUtils::TryCastRank(bot, target, 5176, "Druid", "Wrath"))
            _decisionTimer.Set(1800);
    }

    bool DruidStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);

        if (bot->GetShapeshiftForm() == FORM_BEAR || bot->GetShapeshiftForm() == FORM_DIREBEAR)
        {
            if (bot->IsWithinMeleeRange(threat) && ClassStrategyUtils::TryCastRank(bot, threat, 5211, GetName(), "Bash"))
                return true;
        }

        if (bot->GetShapeshiftForm() == FORM_CAT)
        {
            if (bot->HasSpell(1850) && !bot->HasAura(1850) && ClassStrategyUtils::TryCast(bot, bot, 1850, GetName(), "Dash"))
                return true;
        }

        if (dist <= 8.0f && bot->HasSpell(20549) && ClassStrategyUtils::TryCast(bot, bot, 20549, GetName(), "War Stomp"))
            return true;

        if (bot->GetShapeshiftForm() == FORM_NONE && dist >= 8.0f && dist <= 30.0f &&
            ClassStrategyUtils::TryCastRank(bot, threat, 339, GetName(), "Entangling Roots"))
            return true;

        return false;
    }
}
