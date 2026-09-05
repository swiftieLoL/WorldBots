#include "PriestStrategy.h"
#include "Combat/CombatPositioning.h"
#include "ClassStrategyUtils.h"

namespace Combat
{
    void PriestStrategy::ExecuteCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {

        uint8_t healthPct = blackboard.self.healthPct;

        // 1. Emergency Self Healing Priority
        if (healthPct < 50 && _decisionTimer.IsReady())
        {
            if (!bot->HasAura(6788)) // Weakened Soul
            {
                if (ClassStrategyUtils::TryMaintainAura(bot, bot, 17, GetName(), "Power Word: Shield", _decisionTimer, 1500))
                    return;
            }

            uint32_t healSpell = Helper::SpellUtils::IsSpellReady(bot, 2061) ? 2061
                : (Helper::SpellUtils::IsSpellReady(bot, 2050) ? 2050 : 0);
            if (healSpell != 0)
            {
                if (movement) movement->Stop();
                const char* healName = (healSpell == 2061) ? "Flash Heal" : "Lesser Heal";
                if (ClassStrategyUtils::TryCastRank(bot, bot, healSpell, GetName(), healName))
                {
                    _decisionTimer.Set(2500);
                    return;
                }
            }
        }

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 0.0f, 28.0f);

        if (range != RangeAdjustment::Hold)
            return;

        if (bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
            return;

        // 2. Shadow Word: Pain dot maintenance
        if (ClassStrategyUtils::TryMaintainAura(bot, target, 589, GetName(), "Shadow Word: Pain", _decisionTimer, 1500))
            return;

        // 3. Smite nuke
        if (ClassStrategyUtils::TryCastRank(bot, target, 585, GetName(), "Smite"))
        {
            _decisionTimer.Set(1500);
            return;
        }
    }

    bool PriestStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);
        if (dist <= 8.0f && ClassStrategyUtils::TryCastRank(bot, threat, 8122, GetName(), "Psychic Scream"))
            return true;

        if (!bot->HasAura(6788) && ClassStrategyUtils::TryCastRank(bot, bot, 17, GetName(), "Power Word: Shield"))
            return true;

        return false;
    }
}
