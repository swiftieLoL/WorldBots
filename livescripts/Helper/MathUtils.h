#pragma once

#include <cmath>
#include <random>
#include <algorithm>

class Map;

namespace Helper
{
    inline float DistanceSq(float ax, float ay, float az, float bx, float by, float bz)
    {
        float x = ax - bx;
        float y = ay - by;
        float z = az - bz;
        return x * x + y * y + z * z;
    }

    inline float Distance(float ax, float ay, float az, float bx, float by, float bz)
    {
        return std::sqrt(DistanceSq(ax, ay, az, bx, by, bz));
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

    inline bool IsIn3DRange(float ax, float ay, float az, float bx, float by, float bz, float maxDistance)
    {
        return DistanceSq(ax, ay, az, bx, by, bz) <= (maxDistance * maxDistance);
    }

    inline bool IsIn2DRange(float ax, float ay, float bx, float by, float maxDistance)
    {
        return DistanceSq2D(ax, ay, bx, by) <= (maxDistance * maxDistance);
    }

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
