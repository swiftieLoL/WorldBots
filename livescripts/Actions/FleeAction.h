#pragma once

#include "BaseBotAction.h"
#include "ObjectGuid.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include <memory>

namespace Actions
{
    class FleeAction : public BaseBotAction
    {
    public:
        explicit FleeAction(ObjectGuid threatGuid);

        const char* GetName() const override { return "FleeAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        ObjectGuid GetRelatedTargetGuid() const override { return _threatGuid; }

    private:
        ObjectGuid _threatGuid;
        uint32_t _elapsedMs = 0;
        uint32_t _combatClearMs = 0;
        uint32_t _pathRetryMs = 0;
        uint32_t _disengageCooldownMs = 0;
        bool _sawCombat = false;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
    };
}
