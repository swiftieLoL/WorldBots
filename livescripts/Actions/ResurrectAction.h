#pragma once

#include "BaseBotAction.h"

namespace Actions
{
    class ResurrectAction : public BaseBotAction
    {
    public:
        ResurrectAction();

        const char* GetName() const override { return "ResurrectAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;
    };
}
