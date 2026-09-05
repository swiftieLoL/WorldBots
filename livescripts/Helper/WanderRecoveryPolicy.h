#pragma once

#include <cstdint>

namespace Helper
{
    class WanderRecoveryPolicy
    {
    public:
        static constexpr std::uint32_t FailureThreshold = 3;

        bool RecordExplorationResult(bool movementStarted)
        {
            if (movementStarted)
            {
                Reset();
                return false;
            }

            if (_failureCount < FailureThreshold)
                ++_failureCount;
            return _failureCount >= FailureThreshold;
        }

        void Reset() { _failureCount = 0; }
        std::uint32_t GetFailureCount() const { return _failureCount; }

    private:
        std::uint32_t _failureCount = 0;
    };

    // A completed Wander recovery belongs to the bot rather than to one
    // short-lived WanderAction. Without a bot-wide pause, returning to the
    // same bind ecology can construct another action and teleport again only
    // seconds later. Keep the fallback available after useful progress, but
    // otherwise let destination suppressions and other progression work age
    // before trying Wander again. Consecutive recoveries without XP progress
    // increase that window so a successful bind correction cannot become a
    // five-minute teleport loop in an ecology with no usable outbound paths.
    class WanderRecoveryBackoffPolicy
    {
    public:
        static constexpr std::uint32_t BaseBackoffSeconds = 5 * 60;
        static constexpr std::uint32_t MaxBackoffSeconds = 30 * 60;
        static constexpr std::uint32_t MaxRecoveryCount =
            MaxBackoffSeconds / BaseBackoffSeconds;

        std::uint32_t Begin(std::uint32_t nowSec)
        {
            if (_consecutiveRecoveries < MaxRecoveryCount)
                ++_consecutiveRecoveries;
            std::uint32_t backoffSeconds =
                BaseBackoffSeconds * _consecutiveRecoveries;
            _retryAfterSec = nowSec + backoffSeconds;
            return backoffSeconds;
        }

        bool Expire(std::uint32_t nowSec)
        {
            if (_retryAfterSec == 0 || nowSec < _retryAfterSec)
                return false;
            _retryAfterSec = 0;
            return true;
        }

        void RecordProgress()
        {
            _retryAfterSec = 0;
            _consecutiveRecoveries = 0;
        }
        bool IsReady() const { return _retryAfterSec == 0; }
        std::uint32_t GetConsecutiveRecoveryCount() const
        {
            return _consecutiveRecoveries;
        }
        std::uint32_t GetRemaining(std::uint32_t nowSec) const
        {
            return nowSec < _retryAfterSec ? _retryAfterSec - nowSec : 0;
        }

    private:
        std::uint32_t _retryAfterSec = 0;
        std::uint32_t _consecutiveRecoveries = 0;
    };
}
