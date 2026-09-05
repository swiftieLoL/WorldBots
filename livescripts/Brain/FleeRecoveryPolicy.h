#pragma once

#include <cstdint>

namespace Brain
{
    // A single flee timeout can be a healthy chain pull: the bot may still be
    // moving and combat may legitimately need more than one short escape leg.
    // Repeated timeouts from the same small origin, however, prove that the
    // escape action is being recreated without making spatial progress.
    class FleeRecoveryPolicy
    {
    public:
        static constexpr std::uint32_t FailureThreshold = 6;
        static constexpr std::uint32_t FailureWindowSeconds = 90;
        static constexpr std::uint32_t RecoveryCooldownSeconds = 60;
        static constexpr float FailureOriginRadius = 15.0f;

        bool RecordTimeout(std::uint32_t nowSec, std::uint32_t mapId,
            float x, float y, float z)
        {
            if (nowSec < _recoveryCooldownUntilSec)
                return false;

            const float dx = x - _originX;
            const float dy = y - _originY;
            const float dz = z - _originZ;
            const bool sameOrigin = mapId == _failureMapId &&
                dx * dx + dy * dy + dz * dz <=
                    FailureOriginRadius * FailureOriginRadius;
            if (_failureCount == 0 || nowSec < _windowStartedSec ||
                nowSec - _windowStartedSec > FailureWindowSeconds ||
                !sameOrigin)
            {
                BeginWindow(nowSec, mapId, x, y, z);
            }
            else
            {
                ++_failureCount;
            }

            if (_failureCount < FailureThreshold)
                return false;

            ResetWindow();
            _recoveryCooldownUntilSec = nowSec + RecoveryCooldownSeconds;
            return true;
        }

        void RecordSuccess()
        {
            ResetWindow();
        }

        std::uint32_t GetFailureCount() const { return _failureCount; }

        std::uint32_t GetRecoveryCooldownRemaining(std::uint32_t nowSec) const
        {
            return nowSec < _recoveryCooldownUntilSec
                ? _recoveryCooldownUntilSec - nowSec : 0;
        }

    private:
        void BeginWindow(std::uint32_t nowSec, std::uint32_t mapId,
            float x, float y, float z)
        {
            _windowStartedSec = nowSec;
            _failureMapId = mapId;
            _originX = x;
            _originY = y;
            _originZ = z;
            _failureCount = 1;
        }

        void ResetWindow()
        {
            _windowStartedSec = 0;
            _failureMapId = 0;
            _originX = 0.0f;
            _originY = 0.0f;
            _originZ = 0.0f;
            _failureCount = 0;
        }

        std::uint32_t _windowStartedSec = 0;
        std::uint32_t _failureMapId = 0;
        float _originX = 0.0f;
        float _originY = 0.0f;
        float _originZ = 0.0f;
        std::uint32_t _failureCount = 0;
        std::uint32_t _recoveryCooldownUntilSec = 0;
    };
}
