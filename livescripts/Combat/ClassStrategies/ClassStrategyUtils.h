#pragma once

#include "AI/CreatureAI.h"
#include "Creature.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/MovementManager.h"
#include "Helper/SpellUtils.h"
#include "Helper/CommonTypes.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "Spell.h"
#include "Unit.h"
#include <span>

namespace Combat::ClassStrategyUtils
{
    inline bool IsValid(Player* bot, Unit* target)
    {
        return bot && bot->IsInWorld() && target && target->IsInWorld() &&
            target->IsAlive() && target->GetMap() == bot->GetMap();
    }

    inline bool TryCast(Player* bot, Unit* target, uint32_t spellId,
        const char* strategyName, const char* abilityName)
    {
        if (!spellId || !Helper::SpellUtils::IsSpellReady(bot, spellId) ||
            !Helper::SpellUtils::TryCast(bot, target, spellId))
            return false;

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Combat] {} Bot '{}' cast {} (Spell {})",
                strategyName, bot->GetName(), abilityName, spellId);
        }
        return true;
    }

    inline bool TryCastRank(Player* bot, Unit* target, uint32_t baseSpellId,
        const char* strategyName, const char* abilityName)
    {
        return TryCast(bot, target, Helper::SpellUtils::FindReadyRank(bot, baseSpellId),
            strategyName, abilityName);
    }

    inline void EnsureMeleeAttack(Player* bot, Unit* target)
    {
        if (bot && target && (!bot->GetVictim() || bot->GetVictim() != target || !bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING)))
            bot->Attack(target, true);
    }

    inline bool MaintainMelee(Player* bot, Unit* target, MovementManager* movement)
    {
        if (!IsValid(bot, target))
            return false;

        EnsureMeleeAttack(bot, target);
        if (bot->IsNonMeleeSpellCast(false))
            return true;

        if (!bot->IsWithinMeleeRange(target))
        {
            if (movement)
                movement->MoveTo(target->GetPositionX(), target->GetPositionY(),
                    target->GetPositionZ(), BotMovementState::Moving, false);
            return false;
        }

        if (movement)
            movement->Stop();
        bot->SetInFront(target);
        return true;
    }

    inline void EngagePet(Player* bot, Unit* target)
    {
        if (!bot || !target)
            return;

        Pet* pet = bot->GetPet();
        if (!pet || !pet->IsAlive() || !pet->IsInWorld() || pet->GetMap() != bot->GetMap() ||
            pet->GetVictim() == target)
            return;

        if (CharmInfo* charmInfo = pet->GetCharmInfo())
            charmInfo->SetIsCommandAttack(true);
        if (pet->AI())
            pet->AI()->AttackStart(target);
    }

    inline bool EnsureAutoShot(Player* bot, Unit* target)
    {
        if (!bot || !target)
            return false;

        if (Spell* current = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        {
            if (current->m_targets.GetUnitTargetGUID() == target->GetGUID())
                return true;
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        }

        return Helper::SpellUtils::TryCast(bot, target, 75);
    }

    inline bool TryMaintainAura(Player* bot, Unit* target, uint32_t baseSpellId,
        const char* strategyName, const char* abilityName,
        Common::CooldownTimer& timer, uint32_t delayMs = 1000)
    {
        if (Helper::SpellUtils::HasAuraInChain(target, baseSpellId))
            return false;
        if (!TryCastRank(bot, target, baseSpellId, strategyName, abilityName))
            return false;
        timer.Set(delayMs);
        return true;
    }

    struct PrioritySpell
    {
        uint32_t baseSpellId;
        const char* name;
        uint32_t decisionDelayMs = 1000;
    };

    inline bool TryCastPriorityList(Player* bot, Unit* target,
        std::span<const PrioritySpell> spells, const char* strategyName,
        Common::CooldownTimer& timer)
    {
        for (const auto& spell : spells)
        {
            if (TryCastRank(bot, target, spell.baseSpellId, strategyName, spell.name))
            {
                timer.Set(spell.decisionDelayMs);
                return true;
            }
        }
        return false;
    }

    inline bool TryInterrupt(Player* bot, Unit* target, uint32_t baseSpellId,
        const char* strategyName, const char* abilityName,
        Common::CooldownTimer& timer, uint32_t delayMs = 500)
    {
        if (!target || !target->IsNonMeleeSpellCast(false))
            return false;
        if (!TryCastRank(bot, target, baseSpellId, strategyName, abilityName))
            return false;
        timer.Set(delayMs);
        return true;
    }

    inline bool TryCastEmergencyHeal(Player* bot, Unit* target)
    {
        if (!IsValid(bot, target))
            return false;

        switch (bot->GetClass())
        {
            case CLASS_PRIEST:
                if (target->HealthBelowPct(35) && !Helper::SpellUtils::HasAuraInChain(target, 17) &&
                    !target->HasAura(6788))
                {
                    if (TryCastRank(bot, target, 17, "Priest", "Power Word: Shield"))
                        return true;
                }
                return TryCastRank(bot, target, 2061, "Priest", "Flash Heal") ||
                       TryCastRank(bot, target, 2050, "Priest", "Lesser Heal");

            case CLASS_PALADIN:
                return TryCastRank(bot, target, 19750, "Paladin", "Flash of Light") ||
                       TryCastRank(bot, target, 635, "Paladin", "Holy Light");

            case CLASS_SHAMAN:
                return TryCastRank(bot, target, 8004, "Shaman", "Lesser Healing Wave") ||
                       TryCastRank(bot, target, 331, "Shaman", "Healing Wave");

            case CLASS_DRUID:
                if (!Helper::SpellUtils::HasAuraInChain(target, 774))
                {
                    if (TryCastRank(bot, target, 774, "Druid", "Rejuvenation"))
                        return true;
                }
                return TryCastRank(bot, target, 5185, "Druid", "Healing Touch");

            default:
                return false;
        }
    }
}
