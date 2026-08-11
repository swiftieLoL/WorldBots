#pragma once

#include "AI/CreatureAI.h"
#include "Creature.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/MovementManager.h"
#include "Helper/SpellUtils.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "Spell.h"
#include "Unit.h"
#include <iterator>

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
        if (bot && target && bot->GetVictim() != target)
            bot->Attack(target, true);
    }

    inline bool MaintainMelee(Player* bot, Unit* target, MovementManager* movement)
    {
        if (!IsValid(bot, target))
            return false;

        EnsureMeleeAttack(bot, target);
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
}
