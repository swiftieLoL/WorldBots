#pragma once

#include "BaseBotAction.h"
#include "ObjectGuid.h"

namespace Actions
{
    class FollowAction : public BaseBotAction
    {
    public:
        FollowAction(ObjectGuid targetGuid, float distance = 2.0f, float angle = 0.0f);

        const char* GetName() const override { return "FollowAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed || !_targetGuid; }

    private:
        void UpdateFollow(Player* bot, MovementManager* movement);
        ObjectGuid _targetGuid;
        float _distance;
        float _angle;
    };
}
