#pragma once

#include <cstdint>
#include <limits>

namespace Helper::TargetApproachProgressPolicy
{
    constexpr std::uint32_t StallTimeoutMs = 8000;
    constexpr std::uint32_t MinimumPathAttempts = 3;
    constexpr float MinimumDistanceImprovement = 2.0f;

    template <typename Key>
    class Tracker
    {
    public:
        bool Observe(const Key& key, float distance,
            std::uint32_t targetHealth, bool engaged,
            bool freshPathAttempt, std::uint32_t deltaMs)
        {
            if (!_hasTarget || key != _targetKey)
            {
                BeginTarget(key, distance, targetHealth);
                return false;
            }

            bool healthProgress = targetHealth < _lastTargetHealth;
            bool distanceProgress = distance <=
                _bestDistance - MinimumDistanceImprovement;
            _lastTargetHealth = targetHealth;

            // Once combat is genuinely established, combat ownership and its
            // damage watchdog decide whether the fight is stalled. This
            // policy only bounds an unproductive approach/opening loop.
            if (engaged || healthProgress || distanceProgress)
            {
                _bestDistance = distance;
                _noProgressMs = 0;
                _pathAttemptsWithoutProgress = 0;
                return false;
            }

            if (freshPathAttempt)
                ++_pathAttemptsWithoutProgress;
            _noProgressMs = deltaMs >
                std::numeric_limits<std::uint32_t>::max() - _noProgressMs
                ? std::numeric_limits<std::uint32_t>::max()
                : _noProgressMs + deltaMs;
            return _noProgressMs >= StallTimeoutMs &&
                _pathAttemptsWithoutProgress >= MinimumPathAttempts;
        }

        void Reset()
        {
            _targetKey = {};
            _bestDistance = 0.0f;
            _lastTargetHealth = 0;
            _noProgressMs = 0;
            _pathAttemptsWithoutProgress = 0;
            _hasTarget = false;
        }

        std::uint32_t GetNoProgressMs() const { return _noProgressMs; }
        std::uint32_t GetPathAttemptCount() const
        {
            return _pathAttemptsWithoutProgress;
        }

    private:
        void BeginTarget(const Key& key, float distance,
            std::uint32_t targetHealth)
        {
            _targetKey = key;
            _bestDistance = distance;
            _lastTargetHealth = targetHealth;
            _noProgressMs = 0;
            _pathAttemptsWithoutProgress = 0;
            _hasTarget = true;
        }

        Key _targetKey{};
        float _bestDistance = 0.0f;
        std::uint32_t _lastTargetHealth = 0;
        std::uint32_t _noProgressMs = 0;
        std::uint32_t _pathAttemptsWithoutProgress = 0;
        bool _hasTarget = false;
    };
}
