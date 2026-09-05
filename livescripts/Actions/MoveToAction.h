#pragma once

#include "BaseBotAction.h"

namespace Actions
{
    class MoveToAction : public BaseBotAction
    {
    public:
        MoveToAction(float x, float y, float z, uint32_t mapId = 0);

        const char* GetName() const override { return "MoveToAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

    private:
        bool TryMove(MovementManager* movement, bool force);
        void RetryMove(MovementManager* movement, uint32_t deltaMs);

        static constexpr uint32_t MaxPathAttempts = 3;
        static constexpr uint32_t RetryTimeoutMs = 10000;

        float _x, _y, _z;
        uint32_t _mapId;
        bool _started;
        uint32_t _pathAttempts = 0;
        uint32_t _retryElapsedMs = 0;
    };
}
