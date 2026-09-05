#pragma once

#include "BaseBotAction.h"

namespace Actions
{
    class IdleAction : public BaseBotAction
    {
    public:
        const char* GetName() const override { return "IdleAction"; }

        void Start(Player* /*bot*/, MovementManager* movement) override
        {
            ResetOutcome();
            if (movement)
            {
                movement->Stop();
            }
        }

        void Update(Player* /*bot*/, MovementManager* /*movement*/, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/) override
        {
        }

        void Stop(Player* /*bot*/, MovementManager* /*movement*/) override
        {
        }
    };
}
