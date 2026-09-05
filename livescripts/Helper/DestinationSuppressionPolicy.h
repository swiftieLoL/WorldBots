#pragma once

#include <cstdint>

namespace Helper::DestinationSuppressionPolicy
{
    // Authored points are stable database locations. A repeated navmesh
    // rejection is durable evidence, while a random exploration point should
    // expire quickly as the bot changes position.
    constexpr std::uint32_t AuthoredDurationSeconds = 900;
    constexpr std::uint32_t ExplorationDurationSeconds = 120;
    constexpr float AuthoredRadius = 30.0f;
    constexpr float ExplorationRadius = 8.0f;

    inline std::uint32_t GetDurationSeconds(bool authoredDestination)
    {
        return authoredDestination ? AuthoredDurationSeconds :
            ExplorationDurationSeconds;
    }

    inline float GetRadius(bool authoredDestination)
    {
        return authoredDestination ? AuthoredRadius : ExplorationRadius;
    }
}
