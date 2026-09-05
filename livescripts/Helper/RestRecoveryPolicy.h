#pragma once

#include <cstdint>

namespace Helper
{
    // A RestAction capability timeout describes the bot's current supplies
    // and regeneration state, not one short-lived action instance. Keep that
    // evidence across replacement actions so a bot cannot sit, time out, and
    // immediately recreate the same ineffective rest every 25 seconds.
    class RestRecoveryBackoffPolicy
    {
    public:
        static constexpr std::uint32_t BackoffSeconds = 5 * 60;

        void Begin(std::uint32_t nowSec)
        {
            _retryAfterSec = nowSec + BackoffSeconds;
        }

        bool Expire(std::uint32_t nowSec)
        {
            if (_retryAfterSec == 0 || nowSec < _retryAfterSec)
                return false;
            _retryAfterSec = 0;
            return true;
        }

        void RecordRecovery() { _retryAfterSec = 0; }
        bool IsReady() const { return _retryAfterSec == 0; }
        std::uint32_t GetRemaining(std::uint32_t nowSec) const
        {
            return nowSec < _retryAfterSec ? _retryAfterSec - nowSec : 0;
        }

    private:
        std::uint32_t _retryAfterSec = 0;
    };
}
