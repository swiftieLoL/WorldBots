#pragma once

#include <cmath>
#include <random>
#include <algorithm>
#if __has_include("Player.h")
#if __has_include("Globals/ObjectMgr.h")
#include "Globals/ObjectMgr.h"
#endif
#include "Player.h"
#include "Map.h"

namespace Helper
{
    inline float GetFloorZ(Player* bot, float x, float y, float z, float maxSearch = 50.0f)
    {
        if (Map* map = bot ? bot->GetMap() : nullptr)
        {
            float floorZ = map->GetHeight(bot->GetPhaseMask(), x, y, z, true, maxSearch);
            if (floorZ > -500.0f && !std::isnan(floorZ))
                return floorZ;
        }
        return z;
    }
}
#endif

namespace Helper
{
    inline float DistanceSq(float ax, float ay, float az, float bx, float by, float bz)
    {
        float x = ax - bx;
        float y = ay - by;
        float z = az - bz;
        return x * x + y * y + z * z;
    }

    inline float DistanceSq2D(float ax, float ay, float bx, float by)
    {
        float x = ax - bx;
        float y = ay - by;
        return x * x + y * y;
    }

    inline float Distance2D(float ax, float ay, float bx, float by)
    {
        return std::sqrt(DistanceSq2D(ax, ay, bx, by));
    }

    inline float Distance3D(float ax, float ay, float az, float bx, float by, float bz)
    {
        return std::sqrt(DistanceSq(ax, ay, az, bx, by, bz));
    }

    inline bool IsIn3DRange(float ax, float ay, float az, float bx, float by, float bz, float maxDistance)
    {
        return DistanceSq(ax, ay, az, bx, by, bz) <= (maxDistance * maxDistance);
    }
}

namespace Helper::HashUtils
{
    constexpr uint64_t FNV1aBasis = 1469598103934665603ULL;
    constexpr uint64_t FNV1aPrime = 1099511628211ULL;

    template <typename T>
    inline void MixHash(uint64_t& hash, T value)
    {
        hash ^= static_cast<uint64_t>(value);
        hash *= FNV1aPrime;
    }
}

namespace Helper
{

    inline void GetRandomPointInAnnulus(float centerX, float centerY, float minRadius, float maxRadius, float& outX, float& outY)
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265358979323846f);
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);

        float angle = angleDist(rng);
        float dist = minRadius + unitDist(rng) * std::max(0.1f, maxRadius - minRadius);
        outX = centerX + dist * std::cos(angle);
        outY = centerY + dist * std::sin(angle);
    }
}
