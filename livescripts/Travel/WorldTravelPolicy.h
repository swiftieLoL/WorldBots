#pragma once

#include <cstdint>

namespace Travel::WorldTravelPolicy
{
    // A normal Wrath hearth cast completes in ten seconds. Allow additional
    // server-side transfer grace, but never retry the same synthetic
    // Hearthstone edge repeatedly inside one WorldTravel journey.
    constexpr std::uint32_t HearthstoneTransitionTimeoutMs = 20 * 1000;

    inline bool HasHearthstoneTimedOut(std::uint32_t stepElapsedMs)
    {
        return stepElapsedMs >= HearthstoneTransitionTimeoutMs;
    }

    inline bool CanPlanHearthstone(bool itemUsable,
        bool failedEarlierInJourney)
    {
        return itemUsable && !failedEarlierInJourney;
    }
}
