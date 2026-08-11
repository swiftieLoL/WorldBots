#pragma once

#include "BotAction.h"
#include "Town/TownPlanning.h"

#include <cstddef>
#include <memory>

namespace Actions
{
    class TownRunAction : public BotAction
    {
    public:
        explicit TownRunAction(Town::Plan plan) : _plan(std::move(plan)) { }

        const char* GetName() const override { return "TownRunAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }
        bool IsInterruptible() const override;
        ActionOutcome GetOutcome() const override { return _outcome; }
        uint32_t GetRelatedQuestId() const override { return _relatedQuestId; }
        uint32_t GetRelatedNpcEntry() const override { return _relatedNpcEntry; }
        bool IsInventoryCapacityFailure() const override { return _inventoryCapacityFailure; }
        const std::string& GetOutcomeReason() const override { return _outcomeReason; }

    private:
        void StartNextStep(Player* bot, MovementManager* movement);
        void Finish(ActionOutcome outcome, std::string reason = {});

        Town::Plan _plan;
        std::size_t _nextStep = 0;
        std::unique_ptr<BotAction> _child;
        bool _completed = false;
        bool _inventoryCapacityFailure = false;
        ActionOutcome _outcome = ActionOutcome::Running;
        uint32_t _relatedQuestId = 0;
        uint32_t _relatedNpcEntry = 0;
        std::string _outcomeReason;
    };
}
