#pragma once

#include "BotAction.h"
#include "ObjectGuid.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include <memory>

namespace Actions
{
    class CombatAction : public BotAction
    {
    public:
        CombatAction(ObjectGuid targetGuid);

        const char* GetName() const override { return "CombatAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override;

    private:
        ObjectGuid _targetGuid;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
        bool _started;
        bool _completed;
    };
}
