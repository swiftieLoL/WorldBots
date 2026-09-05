#pragma once

#include <cstdint>
#include <cmath>

namespace Common
{
    enum class MovementFailsafeReason : uint8_t
    {
        None,
        MovingNoProgress,
        IdleNoProgress,
        HardTimeout
    };

    struct PositionInfo
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32_t mapId = 0;
    };

    struct CooldownTimer
    {
        uint32_t remainingMs = 0;

        void Tick(uint32_t deltaMs)
        {
            remainingMs = (remainingMs > deltaMs) ? (remainingMs - deltaMs) : 0;
        }

        bool IsReady() const
        {
            return remainingMs == 0;
        }

        void Set(uint32_t ms)
        {
            remainingMs = ms;
        }
    };

    struct FailsafeTimer
    {
        uint32_t elapsedMs = 0;
        uint32_t totalElapsedMs = 0;
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastZ = 0.0f;
        bool hasPosition = false;
        MovementFailsafeReason movementFailureReason = MovementFailsafeReason::None;

        bool Check(uint32_t deltaMs, uint32_t movingTimeoutMs, uint32_t idleTimeoutMs, bool isMoving)
        {
            elapsedMs += deltaMs;
            return elapsedMs >= (isMoving ? movingTimeoutMs : idleTimeoutMs);
        }

        // Travel actions may legitimately run for several minutes. Treat the
        // short timeout as a no-progress timeout, rather than an action lifetime.
        // Actual displacement also handles routes which initially move away from
        // the target to reach a bridge, ramp, cave entrance, or navmesh corridor.
        bool CheckMovementProgress(uint32_t deltaMs, uint32_t movingNoProgressTimeoutMs,
            uint32_t idleTimeoutMs, uint32_t hardTimeoutMs, bool isMoving,
            float x, float y, float z)
        {
            totalElapsedMs += deltaMs;

            if (!hasPosition)
            {
                lastX = x;
                lastY = y;
                lastZ = z;
                hasPosition = true;
                elapsedMs = 0;
            }
            else
            {
                float dx = x - lastX;
                float dy = y - lastY;
                float dz = z - lastZ;
                if ((dx * dx + dy * dy + dz * dz) >= 0.25f)
                {
                    lastX = x;
                    lastY = y;
                    lastZ = z;
                    elapsedMs = 0;
                }
                else
                {
                    elapsedMs += deltaMs;
                }
            }

            if (totalElapsedMs >= hardTimeoutMs)
            {
                movementFailureReason = MovementFailsafeReason::HardTimeout;
                return true;
            }
            if (elapsedMs >= (isMoving ? movingNoProgressTimeoutMs : idleTimeoutMs))
            {
                movementFailureReason = isMoving
                    ? MovementFailsafeReason::MovingNoProgress
                    : MovementFailsafeReason::IdleNoProgress;
                return true;
            }

            movementFailureReason = MovementFailsafeReason::None;
            return false;
        }

        const char* GetMovementFailureReason() const
        {
            switch (movementFailureReason)
            {
                case MovementFailsafeReason::MovingNoProgress:
                    return "movement path made no positional progress";
                case MovementFailsafeReason::IdleNoProgress:
                    return "movement remained idle without progress";
                case MovementFailsafeReason::HardTimeout:
                    return "travel exceeded the hard action timeout";
                default:
                    return "travel failsafe triggered for an unknown reason";
            }
        }

        void Reset()
        {
            elapsedMs = 0;
            totalElapsedMs = 0;
            hasPosition = false;
            movementFailureReason = MovementFailsafeReason::None;
        }
    };
}
