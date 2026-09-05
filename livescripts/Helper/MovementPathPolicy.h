#pragma once

#include "Helper/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Helper::MovementPathPolicy
{
    // Kept independent from TrinityCore so the route policy can be exercised by
    // the standalone logic tests. MovementManager.cpp statically verifies these
    // values against PathGenerator's PathType constants.
    constexpr std::uint32_t Normal = 0x01;
    constexpr std::uint32_t Shortcut = 0x02;
    constexpr std::uint32_t Incomplete = 0x04;
    constexpr std::uint32_t NoPath = 0x08;
    constexpr std::uint32_t NotUsingPath = 0x10;
    constexpr std::uint32_t Short = 0x20;
    constexpr std::uint32_t FarFromPolyStart = 0x40;
    constexpr std::uint32_t FarFromPolyEnd = 0x80;
    constexpr std::uint32_t FarFromPoly = FarFromPolyStart | FarFromPolyEnd;

    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct CorridorSegment
    {
        Point endpoint;
        bool valid = false;
        bool continues = false;
    };

    struct RecoveryAnchor
    {
        Point point;
        bool valid = false;
    };

    constexpr float MinimumWaypointProgress = 2.0f;

    inline float Distance2D(const Point& left, const Point& right)
    {
        return Helper::Distance2D(left.x, left.y, right.x, right.y);
    }

    inline float Distance3D(const Point& left, const Point& right)
    {
        return Helper::Distance3D(left.x, left.y, left.z, right.x, right.y, right.z);
    }

    inline bool MakesWaypointProgress(const Point& from, const Point& candidate,
        const Point& waypoint, float minimumProgress = MinimumWaypointProgress)
    {
        return Distance2D(candidate, waypoint) <=
            Distance2D(from, waypoint) - minimumProgress;
    }

    class WaypointProgressTracker
    {
    public:
        void Reset()
        {
            _bestDistance = 0.0f;
            _noProgressMs = 0;
            _hasSample = false;
        }

        void Observe(const Point& current, const Point& waypoint, std::uint32_t deltaMs)
        {
            float distance = Distance2D(current, waypoint);
            if (!_hasSample)
            {
                _bestDistance = distance;
                _noProgressMs = 0;
                _hasSample = true;
                return;
            }

            if (distance <= _bestDistance - MinimumWaypointProgress)
            {
                _bestDistance = distance;
                _noProgressMs = 0;
            }
            else
            {
                constexpr std::uint32_t MaximumMs =
                    std::numeric_limits<std::uint32_t>::max();
                _noProgressMs = deltaMs > MaximumMs - _noProgressMs
                    ? MaximumMs : _noProgressMs + deltaMs;
            }
        }

        std::uint32_t GetNoProgressMs() const { return _noProgressMs; }
        float GetBestDistance() const { return _bestDistance; }

    private:
        float _bestDistance = 0.0f;
        std::uint32_t _noProgressMs = 0;
        bool _hasSample = false;
    };

    class TravelProgressTracker
    {
    public:
        void Reset()
        {
            _bestDistance = 0.0f;
            _furthestTopologyDistance = 0.0f;
            _noProgressMs = 0;
            _hasSample = false;
            _hasTopologyAnchor = false;
        }

        void Observe(const Point& current, const Point& waypoint,
            std::uint32_t deltaMs)
        {
            float distance = Distance2D(current, waypoint);
            if (!_hasSample)
            {
                _bestDistance = distance;
                _topologyAnchor = current;
                _furthestTopologyDistance = 0.0f;
                _hasSample = true;
                _hasTopologyAnchor = true;
                return;
            }

            bool closer = distance <= _bestDistance - MinimumWaypointProgress;
            float topologyDistance = _hasTopologyAnchor
                ? Distance3D(current, _topologyAnchor) : 0.0f;
            // Permit a ramp, switchback, or cave corridor to begin by moving
            // away from the destination, but only while it expands into new
            // space. Updating the anchor after every two-yard movement lets a
            // bot oscillate forever inside one small pocket.
            bool expandingTopology = _hasTopologyAnchor &&
                topologyDistance >=
                    _furthestTopologyDistance + MinimumWaypointProgress;
            if (closer)
            {
                _bestDistance = distance;
                _topologyAnchor = current;
                _furthestTopologyDistance = 0.0f;
                _hasTopologyAnchor = true;
                _noProgressMs = 0;
                return;
            }
            if (expandingTopology)
            {
                _furthestTopologyDistance = topologyDistance;
                _noProgressMs = 0;
                return;
            }

            constexpr std::uint32_t MaximumMs =
                std::numeric_limits<std::uint32_t>::max();
            _noProgressMs = deltaMs > MaximumMs - _noProgressMs
                ? MaximumMs : _noProgressMs + deltaMs;
        }

        std::uint32_t GetNoProgressMs() const { return _noProgressMs; }
        float GetBestDistance() const { return _bestDistance; }

    private:
        Point _topologyAnchor;
        float _bestDistance = 0.0f;
        float _furthestTopologyDistance = 0.0f;
        std::uint32_t _noProgressMs = 0;
        bool _hasSample = false;
        bool _hasTopologyAnchor = false;
    };

    inline bool IsUsable(bool calculated, std::uint32_t flags, std::size_t pointCount)
    {
        constexpr std::uint32_t UnsafeFlags = Shortcut | NoPath | NotUsingPath |
            Short | Incomplete | FarFromPoly;
        return calculated && pointCount >= 2 && (flags & Normal) != 0 &&
            (flags & UnsafeFlags) == 0;
    }

    inline bool IsUsableTravelFrontier(bool calculated, std::uint32_t flags,
        std::size_t pointCount)
    {
        // A remote endpoint may be off the current navmesh island while the
        // partial corridor from the bot is still a valid exploration frontier.
        constexpr std::uint32_t UnsafeFlags = Shortcut | NoPath | NotUsingPath |
            Short | FarFromPolyStart;
        return calculated && pointCount >= 2 && (flags & Incomplete) != 0 &&
            (flags & UnsafeFlags) == 0;
    }

    inline bool IsCompleteGroundPath(bool calculated, std::uint32_t flags,
        std::size_t pointCount)
    {
        // Progressive world travel may deliberately consume an incomplete
        // corridor a segment at a time. Fleeing cannot: repeatedly accepting
        // the partial edge of an unreachable destination drives bots into the
        // same hill or navmesh boundary until they become stuck.
        return IsUsable(calculated, flags, pointCount) && (flags & Incomplete) == 0;
    }

    inline bool IsUsableNormalizedEndpoint(bool calculated, std::uint32_t flags,
        std::size_t pointCount, const Point& requested, const Point& normalized,
        float maximumHorizontalCorrection = 4.0f,
        float maximumVerticalCorrection = 50.0f)
    {
        // PathGenerator may attach an authored XYZ to the correct, connected
        // polygon but mark it FARFROMPOLY_END because the DB Z names another
        // vertical layer. Only accept its normalized endpoint when the route
        // itself is complete, the correction stays at the intended XY, and a
        // caller will independently revalidate the corrected ground path.
        constexpr std::uint32_t UnsafeFlags = Shortcut | Incomplete | NoPath |
            NotUsingPath | Short | FarFromPolyStart;
        return calculated && pointCount >= 2 &&
            (flags & Normal) != 0 && (flags & FarFromPolyEnd) != 0 &&
            (flags & UnsafeFlags) == 0 &&
            std::isfinite(requested.x) && std::isfinite(requested.y) &&
            std::isfinite(requested.z) && std::isfinite(normalized.x) &&
            std::isfinite(normalized.y) && std::isfinite(normalized.z) &&
            Distance2D(requested, normalized) <= maximumHorizontalCorrection &&
            std::fabs(requested.z - normalized.z) <= maximumVerticalCorrection;
    }

    inline float ScoreFleeCandidate(float separationGain, float routeLength,
        float uphillRise)
    {
        // Separation is the primary objective. Route length discourages large
        // detours, while a strong uphill penalty makes a similarly safe flat
        // route preferable without banning legitimate ramps outright.
        return separationGain * 4.0f - routeLength * 0.15f - uphillRise * 3.0f;
    }

    inline RecoveryAnchor SelectRecoveryBreadcrumb(
        const std::vector<Point>& breadcrumbs, const Point& current,
        float preferredDistance)
    {
        RecoveryAnchor result;
        if (preferredDistance <= 0.0f)
            return result;

        // A flee leg should remain short enough to be rescored as combat
        // changes, while still reaching a position the bot demonstrably
        // traversed before the encounter.
        float minimumDistance = std::max(4.0f, preferredDistance * 0.4f);
        float maximumDistance = preferredDistance * 1.4f;
        float bestDistance = 0.0f;
        for (const Point& breadcrumb : breadcrumbs)
        {
            float distance = Distance2D(current, breadcrumb);
            if (distance < minimumDistance || distance > maximumDistance ||
                distance <= bestDistance)
            {
                continue;
            }

            result.point = breadcrumb;
            result.valid = true;
            bestDistance = distance;
        }
        return result;
    }

    inline bool IsDistinctEndpoint(const std::vector<Point>& endpoints,
        const Point& candidate, float clusterDistance = 4.0f)
    {
        for (const Point& endpoint : endpoints)
        {
            if (Distance2D(endpoint, candidate) < clusterDistance)
                return false;
        }
        return true;
    }

    inline bool IsDistinctLayeredEndpoint(const std::vector<Point>& endpoints,
        const Point& candidate, float horizontalClusterDistance = 4.0f,
        float verticalClusterDistance = 4.0f)
    {
        for (const Point& endpoint : endpoints)
        {
            if (Distance2D(endpoint, candidate) < horizontalClusterDistance &&
                std::fabs(endpoint.z - candidate.z) < verticalClusterDistance)
            {
                return false;
            }
        }
        return true;
    }

    inline bool IsConfinedEscapeSpace(std::size_t distinctFullDistanceEndpoints)
    {
        // Zero to two endpoint clusters describes a corridor or pocket rather
        // than an open fan. In that topology a radial flee is allowed only
        // when it follows a previously traversed breadcrumb.
        return distinctFullDistanceEndpoints <= 2;
    }

    inline std::vector<Point> CollectBacktrackBreadcrumbs(
        const std::vector<Point>& breadcrumbs, const Point& current,
        float minimumDistance = 4.0f, float maximumDistance = 35.0f)
    {
        std::vector<Point> candidates;
        candidates.reserve(breadcrumbs.size());
        // Breadcrumbs are recorded sequentially (oldest at front, newest at back).
        // Reverse iteration starts from the most recently traversed positions.
        for (auto it = breadcrumbs.rbegin(); it != breadcrumbs.rend(); ++it)
        {
            float distance = Distance2D(current, *it);
            if (distance >= minimumDistance && distance <= maximumDistance)
            {
                candidates.push_back(*it);
            }
        }
        return candidates;
    }

    inline bool ShouldAcceptConfinedRadialEscape(float separationGain,
        float minimumSeparationGain = 5.0f)
    {
        return separationGain >= minimumSeparationGain;
    }

    inline CorridorSegment SelectCorridorSegment(const std::vector<Point>& corridor,
        float maximumDistance, bool allowRevalidatedInterpolation = false)
    {
        CorridorSegment result;
        if (corridor.size() < 2 || maximumDistance <= 0.0f)
            return result;

        // PathGenerator's normal path contains surface-normalized points.
        // Keep one of those proven points as the local endpoint by default.
        // Interpolation is an explicit discovery-only opt-in for callers that
        // independently revalidate the manufactured point as a ground path.
        float traversed = 0.0f;
        std::size_t selectedIndex = 0;
        std::size_t firstProgressFrom = 0;
        std::size_t firstProgressTo = 0;
        for (std::size_t i = 1; i < corridor.size(); ++i)
        {
            const Point& from = corridor[i - 1];
            const Point& to = corridor[i];
            float dx = to.x - from.x;
            float dy = to.y - from.y;
            float dz = to.z - from.z;
            float length = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (length <= 0.001f)
                continue;

            if (firstProgressTo == 0)
            {
                firstProgressFrom = i - 1;
                firstProgressTo = i;
            }

            if (traversed + length > maximumDistance + 0.001f)
                break;

            traversed += length;
            selectedIndex = i;
        }

        if (selectedIndex == 0 && allowRevalidatedInterpolation &&
            firstProgressTo != 0)
        {
            const Point& from = corridor[firstProgressFrom];
            const Point& to = corridor[firstProgressTo];
            float dx = to.x - from.x;
            float dy = to.y - from.y;
            float dz = to.z - from.z;
            float length = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (length > maximumDistance && length > 0.001f)
            {
                float ratio = maximumDistance / length;
                result.endpoint = {
                    from.x + dx * ratio,
                    from.y + dy * ratio,
                    from.z + dz * ratio
                };
                result.valid = true;
                result.continues = true;
                return result;
            }
        }

        if (selectedIndex == 0)
            return result;

        result.endpoint = corridor[selectedIndex];
        result.valid = true;
        result.continues = selectedIndex + 1 < corridor.size();
        return result;
    }
}
