#pragma once

#include "BaseBotAction.h"
#include <memory>
#include <string>
#include <utility>

namespace Actions
{
    class CompositeBotAction : public BaseBotAction
    {
    public:
        ~CompositeBotAction() override = default;

        void Stop(Player* bot, MovementManager* movement) override
        {
            StopChild(bot, movement);
            BaseBotAction::Stop(bot, movement);
        }

        bool IsInterruptible() const override
        {
            return _completed || !_childAction || _childAction->IsInterruptible();
        }

        void OnInterrupted() override
        {
            if (_childAction)
                _childAction->OnInterrupted();
            BaseBotAction::OnInterrupted();
        }

        void Abort(std::string reason, FailureCategory category, RecoveryDirective directive) override
        {
            if (_childAction)
                _childAction->Abort(reason, category, directive);
            BaseBotAction::Abort(std::move(reason), category, directive);
        }

        bool TryUpdateContext(Player* bot, const Blackboard::BotBlackboard& bb) override
        {
            return _childAction && _childAction->TryUpdateContext(bot, bb);
        }

        uint32_t GetRelatedQuestId() const override
        {
            return _childAction ? _childAction->GetRelatedQuestId() : 0;
        }

        uint32_t GetRelatedNpcEntry() const override
        {
            return _childAction ? _childAction->GetRelatedNpcEntry() : 0;
        }

        ObjectGuid GetRelatedTargetGuid() const override
        {
            return _childAction ? _childAction->GetRelatedTargetGuid() : ObjectGuid::Empty;
        }

        bool IsInventoryCapacityFailure() const override
        {
            return _childAction && _childAction->IsInventoryCapacityFailure();
        }

        bool IsWorldTravelInProgress() const override
        {
            return _childAction && _childAction->IsWorldTravelInProgress();
        }

        const char* GetWorldTravelModeName() const override
        {
            return _childAction ? _childAction->GetWorldTravelModeName() : "None";
        }

        const char* GetWorldTravelWaitReasonName() const override
        {
            return _childAction ? _childAction->GetWorldTravelWaitReasonName() : "None";
        }

        uint32_t GetWorldTravelElapsedMs() const override
        {
            return _childAction ? _childAction->GetWorldTravelElapsedMs() : 0;
        }

        uint32_t GetWorldTravelStepElapsedMs() const override
        {
            return _childAction ? _childAction->GetWorldTravelStepElapsedMs() : 0;
        }

        uint32_t GetWorldTravelReplanCount() const override
        {
            return _childAction ? _childAction->GetWorldTravelReplanCount() : 0;
        }

        uint32_t GetWorldTravelStepIndex() const override
        {
            return _childAction ? _childAction->GetWorldTravelStepIndex() : 0;
        }

        uint32_t GetWorldTravelStepCount() const override
        {
            return _childAction ? _childAction->GetWorldTravelStepCount() : 0;
        }

        bool GetTravelFailureArea(Brain::DangerArea& area) const override
        {
            return _childAction && _childAction->GetTravelFailureArea(area);
        }

        bool GetTravelDestination(Common::PositionInfo& destination) const override
        {
            return _childAction && _childAction->GetTravelDestination(destination);
        }

        uint64_t GetProgressActivitySignature() const override
        {
            return _childAction ? _childAction->GetProgressActivitySignature() : 0;
        }

        const std::string& GetOutcomeReason() const override
        {
            return _childAction ? _childAction->GetOutcomeReason() : BaseBotAction::GetOutcomeReason();
        }

    protected:
        void StartChild(std::unique_ptr<BotAction> child, Player* bot, MovementManager* movement)
        {
            StopChild(bot, movement);
            _childAction = std::move(child);
            if (_childAction && bot && movement)
            {
                _childAction->Start(bot, movement);
            }
        }

        void UpdateChild(Player* bot, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
        {
            if (_childAction)
            {
                _childAction->Update(bot, movement, blackboard, deltaMs);
            }
        }

        void StopChild(Player* bot, MovementManager* movement)
        {
            if (_childAction)
            {
                _childAction->Stop(bot, movement);
            }
            _childAction.reset();
        }

        bool HasChild() const { return _childAction != nullptr; }
        bool IsChildComplete() const { return !_childAction || _childAction->IsComplete(); }
        BotAction* GetChild() const { return _childAction.get(); }

        std::unique_ptr<BotAction> _childAction;
    };
}
