#pragma once

#include "BotAction.h"
#include "ObjectGuid.h"

namespace Actions
{
    class FleeAction : public BotAction
    {
    public:
        explicit FleeAction(ObjectGuid threatGuid);

        const char* GetName() const override { return "FleeAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }

    private:
        ObjectGuid _threatGuid;
        uint32_t _elapsedMs = 0;
        bool _completed = false;
    };
}
