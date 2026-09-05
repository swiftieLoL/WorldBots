#pragma once

#include "BaseBotAction.h"

namespace Actions
{
    class RestAction : public BaseBotAction
    {
    public:
        RestAction();

        const char* GetName() const override { return "RestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsInterruptible() const override { return true; }

    private:
        uint32_t _consumeCooldownMs;
        uint32_t _restTimerMs;
    };
}
