#pragma once

#include <cstdint>

namespace Brain
{
    // Exact vendor-spawn suppression handles an isolated bad destination. If
    // several complete TownRun navigation attempts still fail, the bot's
    // current service ecology is the problem: rotating through more cached
    // spawns only consumes progression time. Escalate that repeated evidence
    // to the same procedural friendly-hub relocation used by other recovery.
    class TownServiceRecoveryPolicy
    {
    public:
        static constexpr std::uint32_t FailureThreshold = 3;
        static constexpr std::uint32_t FailureWindowSeconds = 10 * 60;
        static constexpr std::uint32_t RecoveryCooldownSeconds = 60;

        bool RecordNavigationFailure(std::uint32_t nowSec)
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
            _recoveryCooldownUntilSec = nowSec + RecoveryCooldownSeconds;
            return true;
        }

        // A completed service visit or a relocation proves that the preceding
        // destination failures no longer describe the current situation.
        void RecordSuccess()
        {
            ResetFailureWindow();
        }

        std::uint32_t GetFailureCount() const { return _failureCount; }

        std::uint32_t GetRecoveryCooldownRemaining(std::uint32_t nowSec) const
        {
            return nowSec < _recoveryCooldownUntilSec
                ? _recoveryCooldownUntilSec - nowSec : 0;
        }

    private:
        void ResetFailureWindow()
        {
            _windowStartedSec = 0;
            _failureCount = 0;
        }

        std::uint32_t _windowStartedSec = 0;
        std::uint32_t _failureCount = 0;
        std::uint32_t _recoveryCooldownUntilSec = 0;
    };
}
