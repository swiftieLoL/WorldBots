#pragma once

namespace Combat::HunterCombatPolicy
{
    inline constexpr float MinimumRangedDistance = 8.0f;
    // Do not hold at Auto Shot's theoretical outer edge. Weapon reach,
    // target combat reach, and server range checks can leave the repeat spell
    // selected without ever firing there.
    inline constexpr float MaximumReliableRangedDistance = 25.0f;

    constexpr bool ShouldUseMelee(float distance, bool hasUsableAmmunition)
    {
        return distance < MinimumRangedDistance || !hasUsableAmmunition;
    }
}
