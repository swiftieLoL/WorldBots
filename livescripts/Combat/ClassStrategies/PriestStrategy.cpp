#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "PriestStrategy.h"
#include "Combat/CombatPositioning.h"
#include "Helper/SpellUtils.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"

namespace Combat
{
    void PriestStrategy::UpdateCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard,
        uint32_t deltaMs)
    {
        if (!bot || !target || !target->IsAlive()) return;
        ObjectGuid targetGuid = target->GetGUID();

        _cooldownTimer.Tick(deltaMs);

        uint8_t healthPct = blackboard.self.healthPct;

        // 1. Emergency Self Healing Priority
        if (healthPct < 50 && _cooldownTimer.IsReady())
        {
            uint32_t shieldToCast = Helper::SpellUtils::FindReadyRank(bot, 17);
            bool hasShield = Helper::SpellUtils::HasAuraInChain(bot, 17);

            if (shieldToCast != 0 && !hasShield && !bot->HasAura(6788)) // Weakened Soul
            {
                bot->CastSpell(bot, shieldToCast, false);
                _cooldownTimer.Set(1500);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Combat] Priest Bot '{}' cast Power Word: Shield (Spell {})!", bot->GetName(), shieldToCast);
                return;
            }

            uint32_t healSpell = Helper::SpellUtils::FindReadyRank(bot, 2061);

            if (healSpell != 0)
            {
                if (movement) movement->Stop();
                bot->CastSpell(bot, healSpell, false);
                _cooldownTimer.Set(2500);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Combat] Priest Bot '{}' cast Heal Spell ID {}!", bot->GetName(), healSpell);
                return;
            }
        }

        bool hasLineOfSight = CombatPositioning::MaintainRanged(bot, target, movement, 28.0f);

        if (!hasLineOfSight)
            return;

        if (bot->IsNonMeleeSpellCast(false) || !_cooldownTimer.IsReady()) return;

        // 2. Shadow Word: Pain dot maintenance
        uint32_t swpToCast = Helper::SpellUtils::FindReadyRank(bot, 589);
        bool hasSwp = Helper::SpellUtils::HasAuraInChain(target, 589);

        if (swpToCast != 0 && !hasSwp)
        {
            std::string targetName = target->GetName();
            bot->CastSpell(target, swpToCast, false);
            _cooldownTimer.Set(1500);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Combat] Priest Bot '{}' cast Shadow Word: Pain (Spell {}) on '{}'", bot->GetName(), swpToCast, targetName);
            return;
        }

        // 3. Smite nuke
        uint32_t smiteToCast = Helper::SpellUtils::FindReadyRank(bot, 585);

        if (smiteToCast != 0)
        {
            std::string targetName = target->GetName();
            bot->CastSpell(target, smiteToCast, false);
            _cooldownTimer.Set(1500);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Combat] Priest Bot '{}' cast Smite (Spell {}) on '{}'", bot->GetName(), smiteToCast, targetName);
            return;
        }

        if (bot->GetVictim() != target)
        {
            bot->Attack(target, true);
        }
        target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!target || !target->IsAlive()) return;
    }
}
