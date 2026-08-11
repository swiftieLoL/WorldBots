#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "WarriorStrategy.h"
#include "Helper/SpellUtils.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"

namespace Combat
{
    void WarriorStrategy::UpdateCombat(
        Player* bot,
        Unit* target,
        MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/,
        uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !target || !target->IsInWorld() || !target->IsAlive()) return;
        ObjectGuid targetGuid = target->GetGUID();

        if (bot->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_CHARGING) || bot->IsNonMeleeSpellCast(false))
            return;

        _cooldownTimer.Tick(deltaMs);

        float dist = bot->GetDistance(target);
        bool hasLineOfSight = bot->IsWithinLOSInMap(target);

        // Auto Attack target
        if (bot->GetVictim() != target)
        {
            bot->Attack(target, true);
        }
        target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive()) return;

        // 0. Stance Check: Ensure Warrior is in Battle Stance (or Defensive/Berserker if active)
        if (bot->HasSpell(2457) && !bot->HasAura(2457) && !bot->HasAura(71) && !bot->HasAura(2458))
        {
            bot->CastSpell(bot, 2457, false);
        }

        // 1. Charge opener if between 8 and 25 yards (Check Charge BEFORE issuing normal MoveTo)
        if (hasLineOfSight && dist >= 8.0f && dist <= 25.0f && _cooldownTimer.IsReady())
        {
            if (Helper::SpellUtils::IsSpellReady(bot, 100))
            {
                std::string targetName = target->GetName();
                bot->CastSpell(target, 100, false);
                _cooldownTimer.Set(1500);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Combat] Warrior Bot '{}' cast Charge on '{}'", bot->GetName(), targetName);
                return;
            }
        }

        // Positioning: Move to true melee range
        bool inMeleeRange = bot->IsWithinMeleeRange(target);
        if (!inMeleeRange)
        {
            if (movement)
            {
                movement->MoveTo(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), BotMovementState::Moving, false);
            }
        }
        else
        {
            // Facing target at melee range
            bot->SetInFront(target);
        }

        if (!_cooldownTimer.IsReady()) return;

        uint32_t rage = bot->GetPower(POWER_RAGE);

        // 1. Rend if not already active on target (Check all ranks of Rend)
        static const uint32_t rendSpells[] = { 47465, 25208, 11574, 11573, 11572, 6547, 6546, 772 };
        uint32_t rendToCast = Helper::SpellUtils::FindKnownSpell(bot, rendSpells);

        bool hasRendAura = Helper::SpellUtils::HasAnyAura(target, rendSpells);

        if (rendToCast > 0 && !hasRendAura && rage >= 100) // 10 rage in TrinityCore (power = rage * 10)
        {
            std::string targetName = target->GetName();
            bot->CastSpell(target, rendToCast, false);
            _cooldownTimer.Set(1500);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Combat] Warrior Bot '{}' cast Rend (Spell {}) on '{}'", bot->GetName(), rendToCast, targetName);
            return;
        }

        // 2. Victory Rush if proc buff is active (Spell 34428 / Proc 32216)
        if (bot->HasSpell(34428) && (bot->HasAura(34428) || bot->HasAura(32216)))
        {
            std::string targetName = target->GetName();
            bot->CastSpell(target, 34428, false);
            bot->RemoveAura(34428);
            bot->RemoveAura(32216);
            _cooldownTimer.Set(1500);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Combat] Warrior Bot '{}' cast Victory Rush on '{}'", bot->GetName(), targetName);
            return;
        }

        // 3. Heroic Strike if rage > 150 (15 rage)
        static const uint32_t heroicStrikeSpells[] = { 47450, 47449, 30324, 29707, 25286, 11566, 11565, 11564, 1608, 285, 284, 78 };
        uint32_t hsToCast = Helper::SpellUtils::FindKnownSpell(bot, heroicStrikeSpells);

        if (hsToCast > 0 && rage >= 150)
        {
            std::string targetName = target->GetName();
            bot->CastSpell(target, hsToCast, false);
            _cooldownTimer.Set(1500);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Combat] Warrior Bot '{}' cast Heroic Strike (Spell {}) on '{}'", bot->GetName(), hsToCast, targetName);
            return;
        }
    }
}
