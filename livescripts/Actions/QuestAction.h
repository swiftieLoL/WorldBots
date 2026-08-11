#pragma once

#include "BotAction.h"
#include "Blackboard/BotBlackboard.h"
#include "ObjectGuid.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include <memory>
#include <string>

namespace Actions
{
    class ProgressQuestAction : public BotAction
    {
    public:
        explicit ProgressQuestAction(uint32_t questId);

        const char* GetName() const override { return "ProgressQuestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }
        ActionOutcome GetOutcome() const override { return _outcome; }
        uint32_t GetRelatedQuestId() const override { return _lockedQuestId; }
        const std::string& GetOutcomeReason() const override { return _outcomeReason; }

    private:
        bool HandleCombatTarget(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, Unit* target, uint32_t deltaMs);
        bool FindAndEngageObjectiveMob(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active, Quest const* qTemplate, float searchRadius, uint32_t deltaMs);
        bool FindAndUseObjectiveGameObject(Player* bot, MovementManager* movement, const Blackboard::ActiveQuest& active);
        void WanderObjectiveArea(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active, Quest const* qTemplate, float searchRadius, uint32_t deltaMs);
        void Finish(ActionOutcome outcome, std::string reason = {});
        uint64_t CalculateProgressSignature(const Blackboard::ActiveQuest& active) const;

        bool _completed;
        uint32_t _lockedQuestId;
        ActionOutcome _outcome = ActionOutcome::Running;
        std::string _outcomeReason;
        ObjectGuid _objectiveTargetGuid;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
        uint8_t _lastProgressSubPath;
        uint32_t _searchExpandTimerMs;
        uint8_t _searchExpandCount;
        uint32_t _deliveryRetryLogTimerMs;
        uint32_t _sourceRecoveryCooldownMs = 0;
        uint32_t _noProgressTimerMs = 0;
        uint32_t _targetAcquireTimerMs = 0;
        uint32_t _interactionRetryTimerMs = 0;
        uint32_t _creatureSearchCooldownMs = 0;
        uint32_t _gameObjectSearchCooldownMs = 0;
        uint64_t _lastProgressSignature = 0;
    };

    class AcceptQuestAction : public BotAction
    {
    public:
        explicit AcceptQuestAction(uint32_t questId) : _selectedQuestId(questId) { }

        const char* GetName() const override { return "AcceptQuestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }
        bool IsInterruptible() const override { return _completed; }
        ActionOutcome GetOutcome() const override { return _outcome; }
        uint32_t GetRelatedQuestId() const override { return _selectedQuestId; }
        const std::string& GetOutcomeReason() const override { return _outcomeReason; }

    private:
        bool _completed;
        uint32_t _selectedQuestId = 0;
        ActionOutcome _outcome = ActionOutcome::Running;
        std::string _outcomeReason;
        Common::FailsafeTimer _failsafe;
    };

    class TurnInQuestAction : public BotAction
    {
    public:
        explicit TurnInQuestAction(uint32_t questId) : _selectedQuestId(questId) { }

        const char* GetName() const override { return "TurnInQuestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }
        bool IsInterruptible() const override { return _completed; }
        ActionOutcome GetOutcome() const override { return _outcome; }
        uint32_t GetRelatedQuestId() const override { return _selectedQuestId; }
        const std::string& GetOutcomeReason() const override { return _outcomeReason; }

    private:
        bool _completed;
        uint32_t _selectedQuestId = 0;
        ActionOutcome _outcome = ActionOutcome::Running;
        std::string _outcomeReason;
        Common::FailsafeTimer _failsafe;
        uint32_t _retryLogTimerMs;
    };
}
