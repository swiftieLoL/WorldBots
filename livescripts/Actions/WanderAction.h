#pragma once

#include "BotAction.h"
#include <unordered_map>

namespace Actions
{
    class WanderAction : public BotAction
    {
    public:
        WanderAction(float originX, float originY, float originZ, float radius,
            std::unordered_map<uint32_t, uint32_t> suppressedQuests = {});

        const char* GetName() const override { return "WanderAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }

    private:
        float _originX, _originY, _originZ;
        float _radius; // base radius
        float _searchRadius; // expands when no quests found
        uint32_t _pauseTimer;
        bool _completed = false;
        std::unordered_map<uint32_t, uint32_t> _suppressedQuests;
    };
}
