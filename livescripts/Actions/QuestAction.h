#pragma once

#include "BaseBotAction.h"
#include "Blackboard/BotBlackboard.h"
#include "ObjectGuid.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include "Brain/ObservationGrace.h"
#include "Brain/SuppressionRegistry.h"
#include "Helper/RepeatedPathFailurePolicy.h"
#include "Helper/TargetApproachProgressPolicy.h"
#include "Travel/WorldTravel.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Actions
{
    class ProgressQuestAction : public BaseBotAction
    {
    public:
        explicit ProgressQuestAction(uint32_t questId,
            std::vector<Brain::DangerArea> dangerAreas = {});

        const char* GetName() const override { return "ProgressQuestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        uint32_t GetRelatedQuestId() const override { return _lockedQuestId; }
        uint32_t GetRetryDelaySeconds() const override { return _retryDelaySeconds; }
        bool IsWorldTravelInProgress() const override { return _worldTravel.IsActive(); }
        const char* GetWorldTravelModeName() const override { return _worldTravel.GetCurrentModeName(); }
        const char* GetWorldTravelWaitReasonName() const override { return _worldTravel.GetWaitReasonName(); }
        uint32_t GetWorldTravelElapsedMs() const override { return _worldTravel.GetElapsedMs(); }
        uint32_t GetWorldTravelStepElapsedMs() const override { return _worldTravel.GetStepElapsedMs(); }
        uint32_t GetWorldTravelReplanCount() const override { return _worldTravel.GetReplanCount(); }
        uint32_t GetWorldTravelStepIndex() const override { return _worldTravel.GetStepIndex(); }
        uint32_t GetWorldTravelStepCount() const override { return _worldTravel.GetStepCount(); }
        bool GetTravelFailureArea(Brain::DangerArea& area) const override
        {
            return _worldTravel.GetFailureArea(area);
        }
        bool GetTravelDestination(Common::PositionInfo& destination) const override
        {
            return _worldTravel.GetDestination(destination);
        }
        bool IsInventoryCapacityFailure() const override { return _inventoryCapacityFailure; }
        uint64_t GetProgressActivitySignature() const override { return _progressActivitySignature; }

    private:
        bool HandleCombatTarget(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, Unit* target, uint32_t deltaMs);
        bool FindAndEngageObjectiveMob(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active, Quest const* qTemplate, float searchRadius, uint32_t deltaMs);
        bool FindAndUseObjectiveGameObject(Player* bot, MovementManager* movement, const Blackboard::ActiveQuest& active);
        bool HandleVendorPurchaseObjective(Player* bot, MovementManager* movement,
            const Blackboard::ActiveQuest& active);
        bool HandleDeathKnightRunebladeObjective(Player* bot,
            MovementManager* movement, const Blackboard::ActiveQuest& active);
        bool TryMoveToObjective(MovementManager* movement, float x, float y,
            float z, const char* pathSource = "quest_objective");
        bool IsObjectiveTargetSuppressed(Unit* target) const;
        bool HandleRepeatedObjectiveTargetPathFailure(Player* bot,
            MovementManager* movement, Unit* target,
            uint64_t pathGenerationBefore);
        bool HandleObjectiveTargetApproachStall(Player* bot,
            MovementManager* movement, Unit* target,
            uint64_t pathGenerationBefore, uint32_t deltaMs);
        static uint64_t MakeObjectiveDestinationKey(float x, float y, float z);
        void WanderObjectiveArea(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active, Quest const* qTemplate, float searchRadius, uint32_t deltaMs);
        uint64_t CalculateProgressSignature(const Blackboard::ActiveQuest& active) const;

        uint32_t _lockedQuestId;
        ObjectGuid _objectiveTargetGuid;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
        uint8_t _lastProgressSubPath;
        uint32_t _searchExpandTimerMs;
        uint8_t _searchExpandCount;
        uint32_t _sourceRecoveryCooldownMs = 0;
        uint32_t _noProgressTimerMs = 0;
        uint32_t _targetAcquireTimerMs = 0;
        uint32_t _interactionRetryTimerMs = 0;
        uint32_t _creatureSearchCooldownMs = 0;
        uint32_t _gameObjectSearchCooldownMs = 0;
        uint64_t _lastProgressSignature = 0;
        uint64_t _progressActivitySignature = 0;
        uint64_t _lastCombatActivitySignature = 0;
        std::unordered_map<uint64_t, uint32_t> _suppressedObjectiveTargets;
        Helper::RepeatedPathFailurePolicy::Tracker<uint64_t>
            _objectiveTargetPathFailures;
        Helper::RepeatedPathFailurePolicy::Tracker<uint64_t>
            _objectiveDestinationPathFailures;
        Helper::TargetApproachProgressPolicy::Tracker<uint64_t>
            _objectiveTargetApproachProgress;
        uint32_t _retryDelaySeconds = 0;
        bool _inventoryCapacityFailure = false;
        std::vector<Brain::DangerArea> _dangerAreas;
        Travel::WorldTravel _worldTravel;
    };

    class AcceptQuestAction : public BaseBotAction
    {
    public:
        explicit AcceptQuestAction(uint32_t questId,
            std::vector<Brain::DangerArea> dangerAreas = {})
            : _selectedQuestId(questId), _dangerAreas(std::move(dangerAreas)) { }

        const char* GetName() const override { return "AcceptQuestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsInterruptible() const override { return _completed; }
        uint32_t GetRelatedQuestId() const override { return _selectedQuestId; }
        bool IsWorldTravelInProgress() const override { return _worldTravel.IsActive(); }
        const char* GetWorldTravelModeName() const override { return _worldTravel.GetCurrentModeName(); }
        const char* GetWorldTravelWaitReasonName() const override { return _worldTravel.GetWaitReasonName(); }
        uint32_t GetWorldTravelElapsedMs() const override { return _worldTravel.GetElapsedMs(); }
        uint32_t GetWorldTravelStepElapsedMs() const override { return _worldTravel.GetStepElapsedMs(); }
        uint32_t GetWorldTravelReplanCount() const override { return _worldTravel.GetReplanCount(); }
        uint32_t GetWorldTravelStepIndex() const override { return _worldTravel.GetStepIndex(); }
        uint32_t GetWorldTravelStepCount() const override { return _worldTravel.GetStepCount(); }
        bool GetTravelFailureArea(Brain::DangerArea& area) const override
        {
            return _worldTravel.GetFailureArea(area);
        }
        bool GetTravelDestination(Common::PositionInfo& destination) const override
        {
            return _worldTravel.GetDestination(destination);
        }
        static void ClearBotState(ObjectGuid botGuid);
        static void ClearAllState();

    private:
        uint32_t _selectedQuestId = 0;
        Blackboard::KnownQuest _lastKnownQuest;
        bool _hasLastKnownQuest = false;
        Brain::ObservationGrace _availabilityGrace{ 5000 };
        Common::FailsafeTimer _failsafe;
        std::vector<Brain::DangerArea> _dangerAreas;
        Travel::WorldTravel _worldTravel;
    };

    class TurnInQuestAction : public BaseBotAction
    {
    public:
        explicit TurnInQuestAction(uint32_t questId,
            std::vector<Brain::DangerArea> dangerAreas = {})
            : _selectedQuestId(questId), _dangerAreas(std::move(dangerAreas)) { }

        const char* GetName() const override { return "TurnInQuestAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsInterruptible() const override { return _completed; }
        uint32_t GetRelatedQuestId() const override { return _selectedQuestId; }
        bool IsWorldTravelInProgress() const override { return _worldTravel.IsActive(); }
        const char* GetWorldTravelModeName() const override { return _worldTravel.GetCurrentModeName(); }
        const char* GetWorldTravelWaitReasonName() const override { return _worldTravel.GetWaitReasonName(); }
        uint32_t GetWorldTravelElapsedMs() const override { return _worldTravel.GetElapsedMs(); }
        uint32_t GetWorldTravelStepElapsedMs() const override { return _worldTravel.GetStepElapsedMs(); }
        uint32_t GetWorldTravelReplanCount() const override { return _worldTravel.GetReplanCount(); }
        uint32_t GetWorldTravelStepIndex() const override { return _worldTravel.GetStepIndex(); }
        uint32_t GetWorldTravelStepCount() const override { return _worldTravel.GetStepCount(); }
        bool GetTravelFailureArea(Brain::DangerArea& area) const override
        {
            return _worldTravel.GetFailureArea(area);
        }
        bool GetTravelDestination(Common::PositionInfo& destination) const override
        {
            return _worldTravel.GetDestination(destination);
        }

    private:
        uint32_t _selectedQuestId = 0;
        Common::FailsafeTimer _failsafe;
        uint32_t _retryLogTimerMs;
        uint32_t _questGiverResolveElapsedMs = 0;
        std::vector<Brain::DangerArea> _dangerAreas;
        Travel::WorldTravel _worldTravel;
    };
}
