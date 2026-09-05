#pragma once

#include <cstdint>

namespace Helper::HunterPetProvisioningPolicy
{
    inline constexpr uint8_t MinimumLevel = 10;
    inline constexpr uint32_t DefaultCreatureEntry = 3098; // Mottled Boar

    constexpr bool ShouldProvision(bool isHunter, uint8_t level, bool isAlive,
        bool isInCombat, bool hasActivePet, bool hasSavedHunterPet,
        bool provisionAlreadyAttempted)
    {
        return isHunter && level >= MinimumLevel && isAlive && !isInCombat &&
            !hasActivePet && !hasSavedHunterPet && !provisionAlreadyAttempted;
    }
}
