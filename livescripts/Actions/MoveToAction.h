#pragma once

#include "BotAction.h"

namespace Actions
{
    class MoveToAction : public BotAction
    {
    public:
        MoveToAction(float x, float y, float z, uint32_t mapId = 0);

        const char* GetName() const override { return "MoveToAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override;

    private:
        float _x, _y, _z;
        uint32_t _mapId;
        bool _started;
        bool _completed;
    };
}
