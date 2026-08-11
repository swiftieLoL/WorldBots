#pragma once

#include "BotAction.h"

namespace Actions
{
    class IdleAction : public BotAction
    {
    public:
        const char* GetName() const override { return "IdleAction"; }

        void Start(Player* /*bot*/, MovementManager* movement) override
        {
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

        bool IsComplete() const override
        {
            return false;
        }
    };
}
