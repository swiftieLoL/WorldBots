#pragma once

#include <cstdint>

namespace Brain
{
    // A completed Grind failure already represents a wide local search and
    // several rejected hunting anchors. Repeating that whole cycle without XP
    // is evidence that the bot's current ecology/navmesh island is not useful,
    // even when individual short paths elsewhere continue to succeed.
    class ProgressionRecoveryPolicy
    {
    public:
        static constexpr std::uint32_t FailureThreshold = 3;
        static constexpr std::uint32_t FailureWindowSeconds = 10 * 60;
        static constexpr std::uint32_t RecoveryCooldownSeconds = 60;
        static constexpr std::uint32_t StallThresholdSeconds = 30 * 60;
        static constexpr std::uint32_t StallRecoveryCooldownSeconds = 10 * 60;
        static constexpr std::uint32_t MaxStallRecoveriesWithoutProgress = 3;
        static constexpr std::uint32_t GrindRetryBackoffSeconds = 60;
        static constexpr std::uint32_t ExhaustedGrindRetryBackoffSeconds =
            5 * 60;

        bool ObserveProgress(std::uint32_t nowSec, std::uint32_t level,
            std::uint32_t xp)
        {
            if (!_hasProgressSample)
            {
                _hasProgressSample = true;
                _level = level;
                _xp = xp;
                _lastProgressSec = nowSec;
                return false;
            }

            if (nowSec < _lastProgressSec)
                _lastProgressSec = nowSec;

            if (_level == level && _xp == xp)
                return false;

            _level = level;
            _xp = xp;
            _lastProgressSec = nowSec;
            _stallRecoveryCooldownUntilSec = 0;
            _stallRecoveryCount = 0;
            ResetFailureWindow();
            return true;
        }

        // Action-specific policies intentionally reject destination failures:
        // one unreachable quest or vendor must not teleport an otherwise
        // productive bot. A full no-XP interval is the cross-goal evidence
        // that the bot's current progression ecology is genuinely unhelpful.
        bool ShouldRecoverFromStall(std::uint32_t nowSec)
        {
            if (!_hasProgressSample || nowSec < _lastProgressSec ||
                nowSec - _lastProgressSec < StallThresholdSeconds ||
                nowSec < _stallRecoveryCooldownUntilSec ||
                _stallRecoveryCount >= MaxStallRecoveriesWithoutProgress)
            {
                return false;
            }

            ++_stallRecoveryCount;
            _stallRecoveryCooldownUntilSec =
                nowSec + StallRecoveryCooldownSeconds;
            ResetFailureWindow();
            return true;
        }

        bool RecordGrindFailure(std::uint32_t nowSec)
        {
            if (nowSec < _recoveryCooldownUntilSec)
                return false;

            if (_failureCount == 0 || nowSec < _windowStartedSec ||
                nowSec - _windowStartedSec > FailureWindowSeconds)
            {
                _windowStartedSec = nowSec;
                _failureCount = 1;
            }
            else
            {
                ++_failureCount;
            }

            if (_failureCount < FailureThreshold)
                return false;

            ResetFailureWindow();

            // Ecology failures and the cross-goal stall watchdog share one
            // relocation budget. Failed moves and same-ecology bind returns
            // remain charged so rapid complete-Grind failure cycles cannot
            // bypass the bounded no-progress policy. ObserveProgress restores
            // the budget after XP/level gain; RecordRelocationResult restores
            // it only for a materially different, preflighted ecology.
            if (_stallRecoveryCount >= MaxStallRecoveriesWithoutProgress)
                return false;

            ++_stallRecoveryCount;
            _recoveryCooldownUntilSec = nowSec + RecoveryCooldownSeconds;
            return true;
        }

        std::uint32_t GetFailureCount() const { return _failureCount; }

        std::uint32_t GetRecoveryCooldownRemaining(std::uint32_t nowSec) const
        {
            return nowSec < _recoveryCooldownUntilSec
                ? _recoveryCooldownUntilSec - nowSec : 0;
        }

        std::uint32_t GetProgressAge(std::uint32_t nowSec) const
        {
            return _hasProgressSample && nowSec >= _lastProgressSec
                ? nowSec - _lastProgressSec : 0;
        }

        std::uint32_t GetStallRecoveryCount() const
        {
            return _stallRecoveryCount;
        }

        bool IsRecoveryBudgetExhausted() const
        {
            return _stallRecoveryCount >= MaxStallRecoveriesWithoutProgress;
        }

        // Ordinary navigation recovery is insufficient once every bounded
        // progression relocation has failed: it can move the bot between safe
        // hubs without proving that the new ecology can produce XP. Promote
        // the already rate-limited Wander relocation to progression recovery
        // so a validated material ecology change can re-arm this budget.
        bool ShouldPromoteWanderRecoveryToProgression() const
        {
            return IsRecoveryBudgetExhausted();
        }

        std::uint32_t GetGrindRetryBackoffSeconds() const
        {
            return IsRecoveryBudgetExhausted()
                ? ExhaustedGrindRetryBackoffSeconds
                : GrindRetryBackoffSeconds;
        }

        std::uint32_t GetStallRecoveryCooldownRemaining(
            std::uint32_t nowSec) const
        {
            return nowSec < _stallRecoveryCooldownUntilSec
                ? _stallRecoveryCooldownUntilSec - nowSec : 0;
        }

        bool RecordRelocationResult(std::uint32_t nowSec,
            bool materiallyChangedEcology)
        {
            if (!materiallyChangedEcology)
            {
                // The attempt was already charged when recovery triggered.
                // Returning to the same bind area must not manufacture a new
                // budget or refresh the no-progress observation window.
                return false;
            }

            // A genuinely different, preflighted ecology deserves its own
            // observation window. Its first useful XP sample will still run
            // through ObserveProgress and prove the relocation productive.
            _lastProgressSec = nowSec;
            _stallRecoveryCount = 0;
            _recoveryCooldownUntilSec = nowSec + RecoveryCooldownSeconds;
            _stallRecoveryCooldownUntilSec =
                nowSec + StallRecoveryCooldownSeconds;
            ResetFailureWindow();
            return true;
        }

    private:
        void ResetFailureWindow()
        {
            _windowStartedSec = 0;
            _failureCount = 0;
        }

        bool _hasProgressSample = false;
        std::uint32_t _level = 0;
        std::uint32_t _xp = 0;
        std::uint32_t _lastProgressSec = 0;
        std::uint32_t _windowStartedSec = 0;
        std::uint32_t _failureCount = 0;
        std::uint32_t _recoveryCooldownUntilSec = 0;
        std::uint32_t _stallRecoveryCooldownUntilSec = 0;
        std::uint32_t _stallRecoveryCount = 0;
    };
}
