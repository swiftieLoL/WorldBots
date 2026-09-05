#pragma once

#include "Helper/MathUtils.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Helper::OriginPathRecoveryPolicy
{
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    class Tracker
    {
    public:
        static constexpr std::uint32_t FailureThreshold = 3;
        static constexpr std::uint32_t DistinctDestinationThreshold = 2;
        static constexpr std::uint32_t SameDestinationFailureThreshold = 5;
        static constexpr std::uint64_t FailureWindowMs = 10000;
        static constexpr std::uint64_t MinimumEvidenceMs = 1500;
        static constexpr float StableOriginRadius = 4.0f;
        static constexpr float DistinctDestinationRadius = 10.0f;

        bool Observe(std::uint64_t nowMs, std::uint32_t mapId,
            const Point& origin, const Point& destination)
        {
            bool sameWindow = _failureCount != 0 && mapId == _mapId &&
                nowMs >= _windowStartedMs &&
                nowMs - _windowStartedMs <= FailureWindowMs &&
                DistanceSquared(origin, _origin) <=
                    StableOriginRadius * StableOriginRadius;
            if (!sameWindow)
                BeginWindow(nowMs, mapId, origin);

            ++_failureCount;
            bool distinct = true;
            for (std::size_t i = 0; i < _destinationCount; ++i)
            {
                if (DistanceSquared(destination, _destinations[i]) <=
                    DistinctDestinationRadius * DistinctDestinationRadius)
                {
                    distinct = false;
                    break;
                }
            }
            if (distinct && _destinationCount < _destinations.size())
                _destinations[_destinationCount++] = destination;

            std::uint64_t evidenceMs = nowMs >= _windowStartedMs
                ? nowMs - _windowStartedMs : 0;
            bool multipleDestinations =
                _failureCount >= FailureThreshold &&
                _destinationCount >= DistinctDestinationThreshold;
            bool repeatedSameDestination =
                _failureCount >= SameDestinationFailureThreshold;
            _recoveryRequired = _recoveryRequired ||
                (evidenceMs >= MinimumEvidenceMs &&
                 (multipleDestinations || repeatedSameDestination));
            return _recoveryRequired;
        }

        void Reset()
        {
            _windowStartedMs = 0;
            _mapId = 0;
            _origin = {};
            _destinations = {};
            _failureCount = 0;
            _destinationCount = 0;
            _recoveryRequired = false;
        }

        void RecordMovement(std::uint32_t mapId, const Point& position)
        {
            if (_failureCount != 0 &&
                (mapId != _mapId || DistanceSquared(position, _origin) >
                    StableOriginRadius * StableOriginRadius))
            {
                Reset();
            }
        }

        bool IsRecoveryRequired() const { return _recoveryRequired; }
        std::uint32_t GetFailureCount() const { return _failureCount; }
        std::uint32_t GetDistinctDestinationCount() const
        {
            return static_cast<std::uint32_t>(_destinationCount);
        }
        std::uint64_t GetWindowAgeMs(std::uint64_t nowMs) const
        {
            return _failureCount != 0 && nowMs >= _windowStartedMs
                ? nowMs - _windowStartedMs : 0;
        }

    private:
        static float DistanceSquared(const Point& left, const Point& right)
        {
            return Helper::DistanceSq(left.x, left.y, left.z, right.x, right.y, right.z);
        }

        void BeginWindow(std::uint64_t nowMs, std::uint32_t mapId,
            const Point& origin)
        {
            _windowStartedMs = nowMs;
            _mapId = mapId;
            _origin = origin;
            _destinations = {};
            _failureCount = 0;
            _destinationCount = 0;
            _recoveryRequired = false;
        }

        std::uint64_t _windowStartedMs = 0;
        std::uint32_t _mapId = 0;
        Point _origin;
        std::array<Point, DistinctDestinationThreshold> _destinations{};
        std::size_t _destinationCount = 0;
        std::uint32_t _failureCount = 0;
        bool _recoveryRequired = false;
    };

    // A valid navmesh origin can still sit on a tiny disconnected island. It
    // is not enough to reject one destination: that only proves the target is
    // bad. This tracker requires a stable origin to reject a fan of distinct,
    // nearby destinations over several seconds, with no successful path in
    // between. That is procedural evidence that the local island has no exit.
    class DisconnectedTracker
    {
    public:
        static constexpr std::uint32_t DistinctDestinationThreshold = 6;
        static constexpr std::uint64_t FailureWindowMs = 15000;
        static constexpr std::uint64_t MinimumEvidenceMs = 4000;
        static constexpr float StableOriginRadius = 4.0f;
        static constexpr float MinimumProbeDistance = 12.0f;
        static constexpr float MaximumProbeDistance = 60.0f;
        static constexpr float DistinctDestinationRadius = 8.0f;

        bool Observe(std::uint64_t nowMs, std::uint32_t mapId,
            const Point& origin, const Point& destination)
        {
            float probeDistanceSquared = Distance2DSquared(origin, destination);
            if (probeDistanceSquared < MinimumProbeDistance * MinimumProbeDistance ||
                probeDistanceSquared > MaximumProbeDistance * MaximumProbeDistance)
            {
                return _recoveryRequired;
            }

            bool sameWindow = _failureCount != 0 && mapId == _mapId &&
                nowMs >= _windowStartedMs &&
                nowMs - _windowStartedMs <= FailureWindowMs &&
                DistanceSquared(origin, _origin) <=
                    StableOriginRadius * StableOriginRadius;
            if (!sameWindow)
                BeginWindow(nowMs, mapId, origin);

            ++_failureCount;
            bool distinct = true;
            for (std::size_t i = 0; i < _destinationCount; ++i)
            {
                if (Distance2DSquared(destination, _destinations[i]) <=
                    DistinctDestinationRadius * DistinctDestinationRadius)
                {
                    distinct = false;
                    break;
                }
            }
            if (distinct && _destinationCount < _destinations.size())
                _destinations[_destinationCount++] = destination;

            std::uint64_t evidenceMs = nowMs >= _windowStartedMs
                ? nowMs - _windowStartedMs : 0;
            _recoveryRequired = _recoveryRequired ||
                (evidenceMs >= MinimumEvidenceMs &&
                 _destinationCount >= DistinctDestinationThreshold);
            return _recoveryRequired;
        }

        void Reset()
        {
            _windowStartedMs = 0;
            _mapId = 0;
            _origin = {};
            _destinations = {};
            _failureCount = 0;
            _destinationCount = 0;
            _recoveryRequired = false;
        }

        void RecordMovement(std::uint32_t mapId, const Point& position)
        {
            if (_failureCount != 0 &&
                (mapId != _mapId || DistanceSquared(position, _origin) >
                    StableOriginRadius * StableOriginRadius))
            {
                Reset();
            }
        }

        bool IsRecoveryRequired() const { return _recoveryRequired; }
        std::uint32_t GetFailureCount() const { return _failureCount; }
        std::uint32_t GetDistinctDestinationCount() const
        {
            return static_cast<std::uint32_t>(_destinationCount);
        }

    private:
        static float DistanceSquared(const Point& left, const Point& right)
        {
            float dx = left.x - right.x;
            float dy = left.y - right.y;
            float dz = left.z - right.z;
            return dx * dx + dy * dy + dz * dz;
        }

        static float Distance2DSquared(const Point& left, const Point& right)
        {
            float dx = left.x - right.x;
            float dy = left.y - right.y;
            return dx * dx + dy * dy;
        }

        void BeginWindow(std::uint64_t nowMs, std::uint32_t mapId,
            const Point& origin)
        {
            _windowStartedMs = nowMs;
            _mapId = mapId;
            _origin = origin;
            _destinations = {};
            _failureCount = 0;
            _destinationCount = 0;
            _recoveryRequired = false;
        }

        std::uint64_t _windowStartedMs = 0;
        std::uint32_t _mapId = 0;
        Point _origin;
        std::array<Point, DistinctDestinationThreshold> _destinations{};
        std::size_t _destinationCount = 0;
        std::uint32_t _failureCount = 0;
        bool _recoveryRequired = false;
    };
}
