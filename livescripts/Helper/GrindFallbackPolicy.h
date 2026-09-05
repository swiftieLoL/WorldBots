#pragma once

#include <algorithm>
#include <cstdint>

namespace Helper::GrindFallbackPolicy
{
    // Begin by looking just outside the tactical scan, then widen the local
    // hunting area gradually instead of immediately crossing the map.
    constexpr float InitialLocalRadius = 150.0f;
    constexpr float MaximumLocalRadius = 900.0f;
    // Trinity's smooth PathGenerator has a fixed point budget. Requiring a
    // complete end-to-end path for a remote DB spawn therefore rejects valid
    // destinations solely because they are farther away than one query can
    // represent. Local anchors still receive the stronger complete-path
    // preflight; remote anchors must prove each bounded executable leg.
    constexpr float CompletePathPreflightRadius = 180.0f;
    // Widen the same-area search promptly. Waiting until the area-relocation
    // deadline left bots visibly idle for tens of seconds when the nearest
    // level-appropriate creatures were a few hundred yards away.
    constexpr uint32_t LocalExpansionDurationMs = 15000;
    constexpr uint32_t AreaRelocationDelayMs = 60000;
    // Incomplete corridors can keep auto-continuing between the same few
    // frontier points forever while MovementManager remains non-idle.
    constexpr uint32_t HuntingTravelStallTimeoutMs = 20000;
    // Finish before the generic two-minute idle watchdog so Grind can report a
    // meaningful progression failure and activate its normal retry backoff.
    constexpr uint32_t UnproductiveTimeoutMs = 90000;
    // A static anchor rejected by complete-path preflight is durable evidence,
    // unlike arriving while its creature is temporarily absent. Retain hard
    // path failures across Grind/backoff cycles so the bot explores a
    // different ecology instead of retrying the same authored mesh island.
    constexpr uint32_t UnreachableAnchorSuppressionSeconds = 900;
    constexpr uint32_t UnproductiveAnchorSuppressionSeconds = 180;
    constexpr uint32_t AbsentAnchorSuppressionSeconds = 90;
    constexpr uint32_t UnreachableAnchorLimit = 3;
    constexpr uint32_t AbsentAnchorMissLimit = 10;

    inline float GetLocalSearchRadius(uint32_t unproductiveMs)
    {
        float progress = std::min(1.0f,
            static_cast<float>(unproductiveMs) /
                static_cast<float>(LocalExpansionDurationMs));
        return InitialLocalRadius +
            (MaximumLocalRadius - InitialLocalRadius) * progress;
    }

    inline bool ShouldRelocateArea(uint32_t unproductiveMs)
    {
        return unproductiveMs >= AreaRelocationDelayMs;
    }

    inline bool ShouldRequireCompletePathPreflight(float distance)
    {
        return distance <= CompletePathPreflightRadius;
    }

    inline bool ShouldEndStalledHuntingTravel(uint32_t noProgressMs)
    {
        return noProgressMs >= HuntingTravelStallTimeoutMs;
    }

    inline bool ShouldEndAfterUnreachableAnchor(uint32_t unreachableAnchorCount)
    {
        return unreachableAnchorCount >= UnreachableAnchorLimit;
    }

    inline bool ShouldEndAfterAbsentAnchorMisses(uint32_t missCount)
    {
        return missCount >= AbsentAnchorMissLimit;
    }
}
