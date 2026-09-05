#pragma once

#include "Helper/HunterPetProvisioningPolicy.h"

#include <cstdint>

namespace Helper::HunterPetManagementPolicy
{
    inline constexpr uint8_t MendHealthPct = 70;

    enum class RecoveryAction : uint8_t
    {
        None,
        Call,
        Revive
    };

    constexpr bool IsEligibleOwner(bool isHunter, uint8_t level, bool isAlive)
    {
        return isHunter && level >= HunterPetProvisioningPolicy::MinimumLevel && isAlive;
    }

    constexpr RecoveryAction ChooseRecoveryAction(bool eligibleOwner,
        bool ownerInCombat, bool ownerCasting, bool ownerStopped,
        bool hasActivePet, bool activePetAlive,
        bool hasSavedPet, bool savedPetAlive)
    {
        if (!eligibleOwner || ownerInCombat || ownerCasting || !ownerStopped)
            return RecoveryAction::None;

        if (hasActivePet)
            return activePetAlive ? RecoveryAction::None : RecoveryAction::Revive;

        if (!hasSavedPet)
            return RecoveryAction::None;

        return savedPetAlive ? RecoveryAction::Call : RecoveryAction::Revive;
    }

    constexpr bool ShouldMend(bool eligibleOwner, bool petAlive,
        bool petHealthLow, bool alreadyMending)
    {
        return eligibleOwner && petAlive && petHealthLow && !alreadyMending;
    }

    constexpr bool ShouldEnableAutocast(bool isAutocastable,
        bool isThreatReducingCower)
    {
        return isAutocastable && !isThreatReducingCower;
    }
}
