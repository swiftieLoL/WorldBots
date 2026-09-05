#pragma once

#include <cstdint>

namespace Helper
{
    class CombatProgressWatchdog
    {
    public:
        static constexpr std::uint32_t StallTimeoutMs = 20000;

        void Reset()
        {
            _hasSample = false;
            _lowestTargetHealth = 0;
            _noDamageElapsedMs = 0;
        }

        bool Update(std::uint32_t, std::uint32_t targetHealth,
            std::uint32_t deltaMs)
        {
            if (!_hasSample)
            {
                _hasSample = true;
                _lowestTargetHealth = targetHealth;
                return false;
            }

            // Only a new low-water mark on the target proves that this action
            // is advancing toward a kill.  Damage taken by the bot, or damage
            // that an evading/regenerating target immediately heals, must not
            // keep an otherwise unproductive combat action alive forever.
            if (targetHealth < _lowestTargetHealth)
            {
                _lowestTargetHealth = targetHealth;
                _noDamageElapsedMs = 0;
                return false;
            }

            if (deltaMs >= StallTimeoutMs - _noDamageElapsedMs)
            {
                _noDamageElapsedMs = 0;
                return true;
            }

            _noDamageElapsedMs += deltaMs;
            return false;
        }

        bool UpdateWhileEngaged(bool engaged, std::uint32_t botHealth,
            std::uint32_t targetHealth, std::uint32_t deltaMs)
        {
            if (!engaged)
            {
                Reset();
                return false;
            }
            return Update(botHealth, targetHealth, deltaMs);
        }

        std::uint32_t GetNoDamageElapsedMs() const
        {
            return _noDamageElapsedMs;
        }

    private:
        bool _hasSample = false;
        std::uint32_t _lowestTargetHealth = 0;
        std::uint32_t _noDamageElapsedMs = 0;
    };
}
