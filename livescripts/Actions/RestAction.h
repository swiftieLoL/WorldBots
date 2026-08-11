#pragma once

#include "BotAction.h"

namespace Actions
{
    class RestAction : public BotAction
    {
    public:
        RestAction();

        const char* GetName() const override { return "RestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }
        bool IsInterruptible() const override { return _completed; }

    private:
        bool _completed;
        uint32_t _consumeCooldownMs;
        uint32_t _restTimerMs;
    };
}
