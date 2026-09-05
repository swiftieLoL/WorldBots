#pragma once

#include <cmath>
#include <algorithm>

namespace Helper::VendorSelectionPolicy
{
    // In multi-tier terrain or layered capital cities (e.g. Thunder Bluff bluffs or
    // Ironforge upper rings), purely 2D Euclidean distance causes bots to pick vendors
    // directly above or below them on inaccessible elevation tiers.
    // Beyond typical stairs/ramps (8 yards), scale vertical separation aggressively so
    // walkable same-tier vendors are preferred.
    constexpr float VerticalPenaltyThreshold = 8.0f;
    constexpr float VerticalPenaltyMultiplier = 3.0f;
    constexpr uint32_t MaxVendorCandidateRetries = 3;

    inline float CalculateWeightedDistanceSq(float dx, float dy, float dz)
    {
        float dist2DSq = dx * dx + dy * dy;
        float absDz = std::abs(dz);
        float verticalMultiplier = absDz > VerticalPenaltyThreshold
            ? VerticalPenaltyMultiplier : 1.0f;
        float weightedDz = dz * verticalMultiplier;
        return dist2DSq + weightedDz * weightedDz;
    }
}
