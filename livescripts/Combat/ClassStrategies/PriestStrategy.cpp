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

        static const uint32_t shieldSpells[] = { 48066, 48065, 25218, 10901, 10900, 10899, 10898, 6066, 6065, 3747, 600, 17 };
        static const uint32_t flashHealSpells[] = { 48071, 48070, 25235, 10917, 10916, 10915, 9407, 2061 };
        static const uint32_t swpSpells[] = { 48125, 48123, 25368, 10894, 10893, 10892, 970, 589 };
        static const uint32_t smiteSpells[] = { 48123, 48122, 25364, 10934, 10933, 984, 606, 585 };

        // 1. Emergency Self Healing Priority
        if (healthPct < 50 && _cooldownTimer.IsReady())
        {
            uint32_t shieldToCast = Helper::SpellUtils::FindKnownSpell(bot, shieldSpells);

            bool hasShield = Helper::SpellUtils::HasAnyAura(bot, shieldSpells);

            if (shieldToCast != 0 && !hasShield && !bot->HasAura(6788)) // Weakened Soul
            {
                bot->CastSpell(bot, shieldToCast, false);
                _cooldownTimer.Set(1500);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Combat] Priest Bot '{}' cast Power Word: Shield (Spell {})!", bot->GetName(), shieldToCast);
                return;
            }

            uint32_t healSpell = Helper::SpellUtils::FindReadySpell(bot, flashHealSpells);

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
        uint32_t swpToCast = Helper::SpellUtils::FindKnownSpell(bot, swpSpells);

        bool hasSwp = Helper::SpellUtils::HasAnyAura(target, swpSpells);

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
        uint32_t smiteToCast = Helper::SpellUtils::FindReadySpell(bot, smiteSpells);

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
