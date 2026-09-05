#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

namespace Brain
{
    class DeathRecoveryPolicy
    {
    public:
        static constexpr uint32_t DeathWindowSeconds = 300;
        static constexpr std::size_t DeathCircuitThreshold = 3;
        static constexpr uint32_t DeathRecoverySeconds = 300;
        static constexpr uint32_t DeathRecoveryCooldownSeconds = 300;

        void RecordDeath(uint32_t nowSec)
        {
            _deathTimestamps.erase(
                std::remove_if(_deathTimestamps.begin(), _deathTimestamps.end(),
                    [nowSec](uint32_t t) { return (nowSec - t) > DeathWindowSeconds; }),
                _deathTimestamps.end());
            _deathTimestamps.push_back(nowSec);
        }

        std::size_t GetRecentDeathCount() const { return _deathTimestamps.size(); }

        bool ShouldTriggerCircuitBreaker(uint32_t nowSec) const
        {
            return _deathTimestamps.size() >= DeathCircuitThreshold &&
                   nowSec >= _deathRecoveryCooldownUntilSec;
        }

        void ActivateCircuitBreaker(uint32_t nowSec)
        {
            _deathRecoveryUntilSec = nowSec + DeathRecoverySeconds;
            _deathRecoveryCooldownUntilSec = nowSec + DeathRecoveryCooldownSeconds;
            _deathTimestamps.clear();
        }

        bool IsRecoveryActive(uint32_t nowSec) const
        {
            return _deathRecoveryUntilSec > nowSec;
        }

        uint32_t GetRecoveryRemainingSeconds(uint32_t nowSec) const
        {
            if (nowSec >= _deathRecoveryUntilSec)
                return 0;
            return _deathRecoveryUntilSec - nowSec;
        }

        uint32_t GetDeadlyQuestId() const { return _deadlyQuestId; }
        void SetDeadlyQuestId(uint32_t questId) { _deadlyQuestId = questId; }

    private:
        std::vector<uint32_t> _deathTimestamps;
        uint32_t _deathRecoveryUntilSec = 0;
        uint32_t _deathRecoveryCooldownUntilSec = 0;
        uint32_t _deadlyQuestId = 0;
    };
}
