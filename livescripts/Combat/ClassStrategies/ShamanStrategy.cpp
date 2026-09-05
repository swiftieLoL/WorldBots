#include "ShamanStrategy.h"
#include "ClassStrategyUtils.h"
#include "Combat/CombatPositioning.h"

namespace Combat
{
    void ShamanStrategy::ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 0.0f, 28.0f);
        if (range != RangeAdjustment::Hold || bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        if (blackboard.self.healthPct < 40)
        {
            if (movement) movement->Stop();
            if (ClassStrategyUtils::TryCastRank(bot, bot, 8004, "Shaman", "Lesser Healing Wave") ||
                ClassStrategyUtils::TryCastRank(bot, bot, 331, "Shaman", "Healing Wave"))
            { _decisionTimer.Set(1800); return; }
        }
        if (ClassStrategyUtils::TryMaintainAura(bot, bot, 324, "Shaman", "Lightning Shield", _decisionTimer, 1000))
            return;
        if (ClassStrategyUtils::TryInterrupt(bot, target, 8042, "Shaman", "Earth Shock", _decisionTimer, 700))
            return;
        if (ClassStrategyUtils::TryMaintainAura(bot, target, 8050, "Shaman", "Flame Shock", _decisionTimer, 1000))
            return;
        if (ClassStrategyUtils::TryCastRank(bot, target, 403, "Shaman", "Lightning Bolt"))
            _decisionTimer.Set(1800);
    }

    bool ShamanStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);

        if (dist <= 10.0f && ClassStrategyUtils::TryCastRank(bot, bot, 2484, GetName(), "Earthbind Totem"))
            return true;

        if (dist <= 20.0f && ClassStrategyUtils::TryCastRank(bot, threat, 8056, GetName(), "Frost Shock"))
            return true;

        return false;
    }
}
