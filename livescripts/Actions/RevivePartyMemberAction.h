#pragma once

#include "BaseBotAction.h"
#include "ObjectGuid.h"

namespace Actions
{
    class RevivePartyMemberAction : public BaseBotAction
    {
    public:
        explicit RevivePartyMemberAction(ObjectGuid targetGuid) : _targetGuid(targetGuid) { }

        const char* GetName() const override { return "RevivePartyMemberAction"; }
        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

    private:
        uint32_t GetResurrectionSpell(Player* bot) const;

        ObjectGuid _targetGuid;
        uint32_t _elapsedMs = 0;
        uint32_t _castAttemptElapsedMs = 0;
        bool _castStarted = false;
    };
}
