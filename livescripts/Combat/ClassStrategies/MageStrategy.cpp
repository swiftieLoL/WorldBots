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
            static const uint32_t frostNovaSpells[] = { 42917, 27088, 10230, 8658, 6131, 865, 122 };
            uint32_t novaToCast = Helper::SpellUtils::FindReadySpell(bot, frostNovaSpells);

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

        // Primary Spell Nuke: Multi-rank Frostbolt or Fireball lookup
        static const uint32_t frostboltSpells[] = { 42842, 42841, 38697, 27072, 27071, 10181, 10180, 10179, 8408, 8407, 8406, 7322, 205, 168, 116 };
        static const uint32_t fireballSpells[] = { 42833, 42832, 38692, 27070, 10151, 10150, 10149, 8402, 8401, 8400, 3115, 145, 143, 133 };

        uint32_t spellToCast = Helper::SpellUtils::FindKnownSpell(bot, frostboltSpells);
        if (spellToCast == 0)
        {
            spellToCast = Helper::SpellUtils::FindKnownSpell(bot, fireballSpells);
        }

        if (spellToCast != 0 && Helper::SpellUtils::IsSpellReady(bot, spellToCast))
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
