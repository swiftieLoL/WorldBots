#pragma once

#include "BotAction.h"
#include <cstdint>

namespace Actions
{
    class UnstuckAction : public BotAction
    {
    public:
        UnstuckAction(uint32_t deadlyQuestId = 0);

        const char* GetName() const override { return "UnstuckAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }

    private:
        uint32_t _deadlyQuestId;
        bool _completed;
    };
}
