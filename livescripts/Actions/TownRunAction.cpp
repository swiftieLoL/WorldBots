#include "TownRunAction.h"

#include "QuestAction.h"
#include "VendorAction.h"

namespace Actions
{
    void TownRunAction::Start(Player* bot, MovementManager* movement)
    {
        _nextStep = 0;
        _child.reset();
        _completed = false;
        _inventoryCapacityFailure = false;
        _outcome = ActionOutcome::Running;
        _relatedQuestId = 0;
        _relatedNpcEntry = 0;
        _outcomeReason.clear();

        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "bot or movement manager was unavailable when the town run started");
            return;
        }

        if (_plan.Empty())
        {
            Finish(_plan.blockedByMissingVendor || _plan.blockedByProtectedInventory
                ? ActionOutcome::Blocked
                : ActionOutcome::Succeeded,
                _plan.blockedByMissingVendor ? "town services require a vendor, but no usable vendor is known" :
                (_plan.blockedByProtectedInventory ? "quest reward space cannot be created without destroying protected items" : std::string{}));
            return;
        }

        StartNextStep(bot, movement);
    }

    void TownRunAction::StartNextStep(Player* bot, MovementManager* movement)
    {
        if (_nextStep >= _plan.steps.size())
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }

        Town::Step step = _plan.steps[_nextStep];
        if (step.service == Town::Service::TurnInQuest)
        {
            ++_nextStep;
            _child = std::make_unique<TurnInQuestAction>(step.questId);
        }
        else
        {
            bool requireRepair = false;
            bool requireInventoryProgress = false;
            uint32_t targetFreeSlots = 0;
            while (_nextStep < _plan.steps.size() &&
                   _plan.steps[_nextStep].service != Town::Service::TurnInQuest)
            {
                requireRepair = requireRepair || _plan.steps[_nextStep].service == Town::Service::Repair;
                requireInventoryProgress = requireInventoryProgress ||
                    _plan.steps[_nextStep].service == Town::Service::Sell ||
                    _plan.steps[_nextStep].service == Town::Service::CreateRewardSpace;
                if (_plan.steps[_nextStep].service == Town::Service::CreateRewardSpace)
                    targetFreeSlots = _plan.targetFreeBagSlots;
                ++_nextStep;
            }
            _child = std::make_unique<VendorAction>(targetFreeSlots, requireRepair, requireInventoryProgress);
        }

        _child->Start(bot, movement);
    }

    void TownRunAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (_completed || !_child)
            return;

        _child->Update(bot, movement, blackboard, deltaMs);
        if (!_child->IsComplete())
            return;

        ActionOutcome childOutcome = _child->GetOutcome();
        _relatedQuestId = _child->GetRelatedQuestId();
        _relatedNpcEntry = _child->GetRelatedNpcEntry();
        _inventoryCapacityFailure = _child->IsInventoryCapacityFailure();
        std::string childReason = _child->GetOutcomeReason();
        _child->Stop(bot, movement);
        _child.reset();

        if (childOutcome != ActionOutcome::Succeeded)
        {
            Finish(childOutcome, std::move(childReason));
            return;
        }

        _relatedQuestId = 0;
        _relatedNpcEntry = 0;
        _inventoryCapacityFailure = false;
        StartNextStep(bot, movement);
    }

    void TownRunAction::Stop(Player* bot, MovementManager* movement)
    {
        if (_child)
            _child->Stop(bot, movement);
        else if (movement)
            movement->Stop();
    }

    bool TownRunAction::IsInterruptible() const
    {
        return _completed || !_child || _child->IsInterruptible();
    }

    void TownRunAction::Finish(ActionOutcome outcome, std::string reason)
    {
        _outcome = outcome;
        _outcomeReason = std::move(reason);
        _completed = true;
    }
}
