#pragma once

#include "BotAction.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <memory>

namespace Actions
{
    class GrindAction : public BotAction
    {
    public:
        GrindAction(int32_t minLevelOffset, int32_t maxLevelOffset);

        const char* GetName() const override { return "GrindAction"; }
        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;
        bool IsComplete() const override { return false; }

    private:
        bool IsSafeTarget(Player* bot, Creature* creature) const;
        Creature* SelectTarget(Player* bot, const Blackboard::BotBlackboard& blackboard) const;
        void TravelToHuntingGround(Player* bot, MovementManager* movement);

        int32_t _minLevelOffset;
        int32_t _maxLevelOffset;
        ObjectGuid _targetGuid;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
        uint32_t _targetSearchCooldownMs = 0;
        uint32_t _destinationRefreshMs = 0;
    };
}
