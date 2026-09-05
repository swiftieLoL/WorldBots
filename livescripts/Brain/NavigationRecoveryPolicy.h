#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Brain
{
    class NavigationRecoveryPolicy
    {
    public:
        static constexpr std::uint32_t FailureThreshold = 3;
        static constexpr std::uint32_t SameLegFailureThreshold = 3;
        static constexpr std::uint32_t FailureWindowSeconds = 60;
        static constexpr std::uint32_t RecoveryCooldownSeconds = 60;
        static constexpr float FailureOriginRadius = 10.0f;
        static constexpr float DistinctLegRadius = 10.0f;

        bool RecordFailure(std::uint32_t nowSec, std::uint32_t mapId,
            float originX, float originY, float originZ,
            float legX, float legY, float legZ)
        {
            if (nowSec < _recoveryCooldownUntilSec)
                return false;

            float originDx = originX - _originX;
            float originDy = originY - _originY;
            float originDz = originZ - _originZ;
            bool sameOrigin = mapId == _failureMapId &&
                originDx * originDx + originDy * originDy + originDz * originDz <=
                    FailureOriginRadius * FailureOriginRadius;
            if (_failedLegs.empty() || nowSec < _windowStartedSec ||
                nowSec - _windowStartedSec > FailureWindowSeconds ||
                !sameOrigin)
            {
                BeginWindow(nowSec, mapId, originX, originY, originZ);
            }

            const float distinctRadiusSq = DistinctLegRadius * DistinctLegRadius;
            bool duplicateLeg = std::any_of(_failedLegs.begin(), _failedLegs.end(),
                [=](const FailedLeg& failed) {
                    float dx = legX - failed.x;
                    float dy = legY - failed.y;
                    float dz = legZ - failed.z;
                    return dx * dx + dy * dy + dz * dz <= distinctRadiusSq;
                });
            if (duplicateLeg)
            {
                ++_sameLegFailureCount;
                if (_sameLegFailureCount >= SameLegFailureThreshold)
                {
                    BeginRecoveryCooldown(nowSec);
                    return true;
                }
                return false;
            }

            _failedLegs.push_back({ legX, legY, legZ });
            _sameLegFailureCount = 1;
            if (_failedLegs.size() < FailureThreshold)
                return false;

            BeginRecoveryCooldown(nowSec);
            return true;
        }

        void RecordSuccess()
        {
            ResetWindow();
        }

        void Reset()
        {
            ResetWindow();
            _recoveryCooldownUntilSec = 0;
        }

        std::uint32_t GetRecoveryCooldownRemaining(std::uint32_t nowSec) const
        {
            return nowSec < _recoveryCooldownUntilSec
                ? _recoveryCooldownUntilSec - nowSec : 0;
        }

        std::uint32_t GetFailureCount() const
        {
            return static_cast<std::uint32_t>(_failedLegs.size());
        }

    private:
        struct FailedLeg
        {
            float x;
            float y;
            float z;
        };

        void BeginWindow(std::uint32_t nowSec, std::uint32_t mapId,
            float originX, float originY, float originZ)
        {
            _windowStartedSec = nowSec;
            _failureMapId = mapId;
            _originX = originX;
            _originY = originY;
            _originZ = originZ;
            _sameLegFailureCount = 0;
            _failedLegs.clear();
        }

        void ResetWindow()
        {
            _windowStartedSec = 0;
            _failureMapId = 0;
            _originX = 0.0f;
            _originY = 0.0f;
            _originZ = 0.0f;
            _sameLegFailureCount = 0;
            _failedLegs.clear();
        }

        void BeginRecoveryCooldown(std::uint32_t nowSec)
        {
            ResetWindow();
            _recoveryCooldownUntilSec = nowSec + RecoveryCooldownSeconds;
        }

        std::uint32_t _windowStartedSec = 0;
        std::uint32_t _failureMapId = 0;
        float _originX = 0.0f;
        float _originY = 0.0f;
        float _originZ = 0.0f;
        std::uint32_t _sameLegFailureCount = 0;
        std::uint32_t _recoveryCooldownUntilSec = 0;
        std::vector<FailedLeg> _failedLegs;
    };
}
