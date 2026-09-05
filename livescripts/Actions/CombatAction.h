#pragma once

#include "BaseBotAction.h"
#include "ObjectGuid.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include "Helper/CombatProgressWatchdog.h"
#include "Helper/CombatStallRecoveryPolicy.h"
#include <memory>

class Creature;

namespace Actions
{
    class CombatAction : public BaseBotAction
    {
    public:
        CombatAction(ObjectGuid targetGuid);

        const char* GetName() const override { return "CombatAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool TryUpdateContext(Player* bot, const Blackboard::BotBlackboard& bb) override;
        ObjectGuid GetRelatedTargetGuid() const override { return _targetGuid; }

    private:
        void RecoverFromNoDamageStall(Player* bot, Creature* target,
            MovementManager* movement);

        ObjectGuid _targetGuid;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
        Helper::CombatProgressWatchdog _progressWatchdog;
        Helper::CombatStallRecoveryPolicy _stallRecovery;
        uint32_t _nonEngagementMs = 0;
    };
}
