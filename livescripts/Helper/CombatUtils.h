#pragma once

#include "Creature.h"
#include "Combat/CombatEngagementPolicy.h"
#include "Helper/Constants.h"
#include "Player.h"

namespace Helper::CombatUtils
{
    inline bool IsControlledByBotOrGroupMember(Player* bot, Unit* unit)
    {
        if (!bot || !unit)
            return false;

        Player* controller = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
        return controller &&
            (controller == bot || bot->IsInSameGroupWith(controller));
    }

    enum class TargetValidationResult : uint8_t
    {
        Valid,
        Missing,
        Dead,
        InvalidWorld,
        NotHostile,
        OutOfRange,
        Evading,
        ClaimedByOtherPlayer
    };

    inline const char* TargetValidationResultName(TargetValidationResult result)
    {
        switch (result)
        {
            case TargetValidationResult::Valid: return "valid";
            case TargetValidationResult::Missing: return "missing";
            case TargetValidationResult::Dead: return "dead";
            case TargetValidationResult::InvalidWorld: return "invalid_world";
            case TargetValidationResult::NotHostile: return "not_hostile";
            case TargetValidationResult::OutOfRange: return "out_of_range";
            case TargetValidationResult::Evading: return "evading";
            case TargetValidationResult::ClaimedByOtherPlayer: return "claimed_by_other_player";
            default: return "unknown";
        }
    }

    inline TargetValidationResult ValidateTarget(Player* bot, Creature* target,
        float maxRange = Constants::MaxCombatChaseRange)
    {
        if (!bot || !target)
            return TargetValidationResult::Missing;
        if (!target->IsAlive())
            return TargetValidationResult::Dead;
        if (!bot->IsInWorld() || !target->IsInWorld() || bot->GetMap() != target->GetMap())
            return TargetValidationResult::InvalidWorld;
        Unit* victim = target->GetVictim();
        const bool defendingSelfOrGroup = target->IsInCombatWith(bot) ||
            IsControlledByBotOrGroupMember(bot, victim);
        if (!target->isTargetableForAttack() || !bot->IsValidAttackTarget(target))
            return TargetValidationResult::NotHostile;
        if (target->IsInEvadeMode())
            return TargetValidationResult::Evading;
        if (Combat::ExceedsVoluntaryCombatRange(
            defendingSelfOrGroup, bot->GetDistance(target), maxRange))
            return TargetValidationResult::OutOfRange;

        if (!defendingSelfOrGroup)
        {
            if (victim && !IsControlledByBotOrGroupMember(bot, victim))
                return TargetValidationResult::ClaimedByOtherPlayer;
            if (Player* recipient = target->GetLootRecipient(); recipient &&
                !IsControlledByBotOrGroupMember(bot, recipient))
                return TargetValidationResult::ClaimedByOtherPlayer;
        }

        return TargetValidationResult::Valid;
    }

    inline TargetValidationResult ValidateTarget(Player* bot, Unit* target,
        float maxRange = Constants::MaxCombatChaseRange)
    {
        if (!bot || !target)
            return TargetValidationResult::Missing;
        if (!target->IsAlive())
            return TargetValidationResult::Dead;
        if (!bot->IsInWorld() || !target->IsInWorld() || bot->GetMap() != target->GetMap())
            return TargetValidationResult::InvalidWorld;
        if (Creature* creature = target->ToCreature())
            return ValidateTarget(bot, creature, maxRange);

        if (Player* playerTarget = target->ToPlayer())
        {
            if (!playerTarget->isTargetableForAttack() || !bot->IsValidAttackTarget(playerTarget))
                return TargetValidationResult::NotHostile;
            if (bot->GetDistance(playerTarget) > maxRange)
                return TargetValidationResult::OutOfRange;
            return TargetValidationResult::Valid;
        }

        return TargetValidationResult::NotHostile;
    }
}
