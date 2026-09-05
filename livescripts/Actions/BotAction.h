#pragma once

#include "Actions/ActionTypes.h"
#include "Helper/MovementManager.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <string>

class Player;

namespace Blackboard
{
    struct BotBlackboard;
}

namespace Brain
{
    struct DangerArea;
}

namespace Common
{
    struct PositionInfo;
}

namespace Actions
{
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
        virtual void OnInterrupted() {}
        virtual void Abort(std::string reason, FailureCategory category,
            RecoveryDirective directive) = 0;
        virtual bool TryUpdateContext(Player* /*bot*/, const Blackboard::BotBlackboard& /*bb*/) { return false; }
        virtual ActionOutcome GetOutcome() const { return IsComplete() ? ActionOutcome::Succeeded : ActionOutcome::Running; }
        virtual FailureCategory GetFailureCategory() const { return FailureCategory::None; }
        virtual RecoveryDirective GetRecoveryDirective() const { return RecoveryDirective::None; }
        virtual uint32_t GetRelatedQuestId() const { return 0; }
        virtual uint32_t GetRelatedNpcEntry() const { return 0; }
        virtual ObjectGuid GetRelatedTargetGuid() const { return ObjectGuid::Empty; }
        virtual bool IsInventoryCapacityFailure() const { return false; }
        virtual uint32_t GetRetryDelaySeconds() const { return 0; }
        virtual bool IsWorldTravelInProgress() const { return false; }
        virtual const char* GetWorldTravelModeName() const { return "None"; }
        virtual const char* GetWorldTravelWaitReasonName() const { return "None"; }
        virtual uint32_t GetWorldTravelElapsedMs() const { return 0; }
        virtual uint32_t GetWorldTravelStepElapsedMs() const { return 0; }
        virtual uint32_t GetWorldTravelReplanCount() const { return 0; }
        virtual uint32_t GetWorldTravelStepIndex() const { return 0; }
        virtual uint32_t GetWorldTravelStepCount() const { return 0; }
        virtual bool GetTravelFailureArea(Brain::DangerArea& /*area*/) const
        {
            return false;
        }
        virtual bool GetTravelDestination(Common::PositionInfo& /*destination*/) const
        {
            return false;
        }
        // Non-zero while an action is making measurable progress that is not
        // represented by quest counters yet (for example, damaging a target).
        virtual uint64_t GetProgressActivitySignature() const { return 0; }
        virtual const std::string& GetOutcomeReason() const
        {
            static const std::string emptyReason;
            return emptyReason;
        }
    };
}
