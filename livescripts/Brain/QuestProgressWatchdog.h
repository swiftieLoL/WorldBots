#pragma once

#include <cstdint>

namespace Brain
{
    class QuestProgressWatchdog
    {
    public:
        static constexpr uint32_t WatchdogTimeoutMs = 180000; // 3 minutes without objective/target health progress

        void Update(uint32_t questId, uint64_t signature, bool activelyExecuting, bool travellingToObjective, uint32_t deltaMs)
        {
            if (questId == 0)
            {
                Reset();
                return;
            }

            if (_watchedQuestId != questId || _watchedSignature != signature)
            {
                _watchedQuestId = questId;
                _watchedSignature = signature;
                _noChangeMs = 0;
            }
            else
            {
                if (activelyExecuting && !travellingToObjective)
                    _noChangeMs += deltaMs;

                if (_noChangeMs >= WatchdogTimeoutMs)
                {
                    _isStalled = true;
                }
            }
        }

        bool IsStalled() const { return _isStalled; }

        void ClearStall()
        {
            _isStalled = false;
            Reset();
        }

        void Reset()
        {
            _watchedQuestId = 0;
            _watchedSignature = 0;
            _noChangeMs = 0;
            _isStalled = false;
        }

        uint32_t GetWatchedQuestId() const { return _watchedQuestId; }
        uint32_t GetNoChangeMs() const { return _noChangeMs; }

    private:
        uint32_t _watchedQuestId = 0;
        uint64_t _watchedSignature = 0;
        uint32_t _noChangeMs = 0;
        bool _isStalled = false;
    };
}
