#pragma once

#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Helper/MovementManager.h"
#include <cstdint>
#include <string>

namespace Blackboard
{
    struct BotBlackboard;
}

namespace Actions
{
    enum class ActionOutcome : uint8_t
    {
        Running,
        Succeeded,
        RetryableFailure,
        Blocked,
        Unsupported
    };

    class BotAction
    {
    public:
        virtual ~BotAction() = default;
        virtual const char* GetName() const = 0;

        virtual void Start(Player* bot, MovementManager* movement) = 0;
        virtual void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) = 0;
        virtual void Stop(Player* /*bot*/, MovementManager* movement)
        {
            if (movement)
            {
                movement->Stop();
            }
        }

        virtual bool IsComplete() const = 0;
        virtual bool IsInterruptible() const { return true; }
        virtual ActionOutcome GetOutcome() const { return IsComplete() ? ActionOutcome::Succeeded : ActionOutcome::Running; }
        virtual uint32_t GetRelatedQuestId() const { return 0; }
        virtual uint32_t GetRelatedNpcEntry() const { return 0; }
        virtual bool IsInventoryCapacityFailure() const { return false; }
        virtual const std::string& GetOutcomeReason() const
        {
            static const std::string emptyReason;
            return emptyReason;
        }
    };
}
