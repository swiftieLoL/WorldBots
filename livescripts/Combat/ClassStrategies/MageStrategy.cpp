#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "MageStrategy.h"
#include "Combat/CombatPositioning.h"
#include "Helper/SpellUtils.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"

namespace Combat
{
    void MageStrategy::UpdateCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/,
        uint32_t deltaMs)
    {
        if (!bot || !target || !target->IsAlive()) return;
        ObjectGuid targetGuid = target->GetGUID();

        _cooldownTimer.Tick(deltaMs);

        float dist = bot->GetDistance(target);
        bool hasLineOfSight = CombatPositioning::MaintainRanged(bot, target, movement, 28.0f);

        if (!hasLineOfSight)
            return;

        // Defensive Frost Nova if target gets closer than 8 yards
        if (dist <= 8.0f && _cooldownTimer.IsReady())
        {
            uint32_t novaToCast = Helper::SpellUtils::FindReadyRank(bot, 122);

            if (novaToCast != 0)
            {
                bot->CastSpell(bot, novaToCast, false);
                target = ObjectAccessor::GetUnit(*bot, targetGuid);
                if (!target || !target->IsAlive()) return;
                _cooldownTimer.Set(1500);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Combat] Mage Bot '{}' cast Frost Nova (Spell {})!", bot->GetName(), novaToCast);
                return;
            }
        }

        if (bot->IsNonMeleeSpellCast(false) || !_cooldownTimer.IsReady()) return;

        // Prefer the highest known Frostbolt rank, then fall back to Fireball.
        uint32_t spellToCast = Helper::SpellUtils::FindReadyRank(bot, 116);
        if (spellToCast == 0)
            spellToCast = Helper::SpellUtils::FindReadyRank(bot, 133);

        if (spellToCast != 0)
        {
            std::string targetName = target->GetName();
            bot->CastSpell(target, spellToCast, false);
            _cooldownTimer.Set(1500);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Combat] Mage Bot '{}' cast spell ID {} on '{}'", bot->GetName(), spellToCast, targetName);
            return;
        }

        // Fallback to auto attack if no spell ready
        if (bot->GetVictim() != target)
        {
            bot->Attack(target, true);
        }
        target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!target || !target->IsAlive()) return;
    }
}
