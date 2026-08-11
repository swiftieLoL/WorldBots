#pragma once

#include "BotAction.h"

namespace Actions
{
    class ResurrectAction : public BotAction
    {
    public:
        ResurrectAction();

        const char* GetName() const override { return "ResurrectAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }

    private:
        bool _completed;
    };
}
