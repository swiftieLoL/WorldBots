#pragma once

#include "BotGoal.h"
#include "GoalTier.h"
#include "ActionRequest.h"
#include "Actions/BotAction.h"
#include "Blackboard/BotBlackboard.h"
#include "Helper/MovementManager.h"
#include "Helper/StuckDetector.h"
#include "Brain/SuppressionRegistry.h"
#include "Brain/DeathRecoveryPolicy.h"
#include "Brain/QuestProgressWatchdog.h"
#include "Brain/QuestFailureMemory.h"
#include "Brain/HostileContextTracker.h"
#include "Brain/TravelHazardPolicy.h"
#include "Brain/TransitionMetrics.h"
#include "Brain/NavigationRecoveryPolicy.h"
#include "Brain/FleeRecoveryPolicy.h"
#include "Brain/ProgressionRecoveryPolicy.h"
#include "Brain/TownServiceRecoveryPolicy.h"
#include "Helper/RestRecoveryPolicy.h"
#include "Helper/WanderRecoveryPolicy.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Town/TownPlanning.h"
#include "Factory/BotRoster.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Brain
{
    class BotBrain
    {
    public:
        BotBrain(Player* bot, MovementManager* movement,
            Factory::BehaviorProfile profile = Factory::BehaviorProfile::Balanced);
        ~BotBrain();

        // Stops the active action before the runtime releases its bot context.
        // This is intentionally idempotent so lifecycle transitions can call it
        // before erasing a bot from the central registry.
        void Shutdown();

        void Sense(uint32_t deltaMs);
        void Think(uint32_t deltaMs);
        void UpdateAction(uint32_t deltaMs);

        GoalTier GetActiveTier() const { return _activeTier; }
        const TransitionMetrics& GetTransitionMetrics() const { return _transitionMetrics; }

        BotGoal GetGoal() const { return _goal; }
        std::string GetGoalString() const;
        std::string GetActionString() const;
        std::string GetActionDiagnosticDetail() const;
        const char* GetBehaviorProfileName() const { return Factory::BehaviorProfileName(_profile); }
        uint32_t GetActiveQuestId() const { return _activeQuestId; }
        uint32_t GetQuestProgressWatchId() const { return _progressWatchdog.GetWatchedQuestId(); }
        uint32_t GetQuestProgressWatchElapsedMs() const { return _progressWatchdog.GetNoChangeMs(); }
        uint32_t GetGrindUntilLevel() const;
        uint32_t GetConservativeGrindUntilLevel() const { return _conservativeGrindUntilLevel; }
        uint32_t GetInventoryCleanupBlockedFreeSlots() const { return _inventoryCleanupBlockedFreeSlots; }
        uint32_t GetInventoryCleanupRetryRemainingSeconds() const;
        uint32_t GetQuestSuppressionRemainingSeconds(uint32_t questId) const;
        uint32_t GetQuestRetryLevel(uint32_t questId) const;
        uint32_t GetQuestFailureCount(uint32_t questId) const;
        bool IsQuestSessionBlocked(uint32_t questId) const;
        uint32_t GetNpcSuppressionRemainingSeconds(uint32_t npcEntry) const;
        uint32_t GetDeathRecoveryRemainingSeconds() const;
        std::vector<std::pair<uint32_t, uint32_t>> GetSuppressedQuests() const;
        std::string GetActionOutcomeReason() const;
        std::string GetPreviousActionOutcome() const;
        uint32_t GetActiveActionElapsedMs() const { return _activeActionElapsedMs; }
        uint32_t GetActiveActionIdleMs() const { return _activeActionIdleMs; }
        bool HasFreshActionPathEvidence() const
        {
            return _movement && _movement->GetPathAttemptGeneration() !=
                _activeActionPathAttemptGeneration;
        }
        const char* GetWorldTravelModeName() const
        {
            return _activeAction ? _activeAction->GetWorldTravelModeName() : "None";
        }
        const char* GetWorldTravelWaitReasonName() const
        {
            return _activeAction ? _activeAction->GetWorldTravelWaitReasonName() : "None";
        }
        uint32_t GetWorldTravelElapsedMs() const
        {
            return _activeAction ? _activeAction->GetWorldTravelElapsedMs() : 0;
        }
        uint32_t GetWorldTravelStepElapsedMs() const
        {
            return _activeAction ? _activeAction->GetWorldTravelStepElapsedMs() : 0;
        }
        uint32_t GetWorldTravelReplanCount() const
        {
            return _activeAction ? _activeAction->GetWorldTravelReplanCount() : 0;
        }
        uint32_t GetWorldTravelStepIndex() const
        {
            return _activeAction ? _activeAction->GetWorldTravelStepIndex() : 0;
        }
        uint32_t GetWorldTravelStepCount() const
        {
            return _activeAction ? _activeAction->GetWorldTravelStepCount() : 0;
        }

        Player* GetBot() const;
        ObjectGuid GetBotGuid() const { return _botGuid; }
        const Blackboard::BotBlackboard& GetBlackboard() const { return _blackboard; }
        Town::Plan PreviewTownPlan() const;

        SuppressionRegistry& GetSuppressions() { return _suppressions; }
        const SuppressionRegistry& GetSuppressions() const { return _suppressions; }
        DeathRecoveryPolicy& GetDeathRecovery() { return _deathRecovery; }
        const DeathRecoveryPolicy& GetDeathRecovery() const { return _deathRecovery; }
        QuestProgressWatchdog& GetProgressWatchdog() { return _progressWatchdog; }
        const QuestProgressWatchdog& GetProgressWatchdog() const { return _progressWatchdog; }
        HostileContextTracker& GetHostileContext() { return _hostileContext; }
        const HostileContextTracker& GetHostileContext() const { return _hostileContext; }

    private:
        Player* ResolveBot() const;
        bool IsBlackboardDecisionReady() const;
        bool IsCreatureRequiredByActiveQuest(uint32_t creatureEntry) const;
        QuestFailureDecision RememberQuestFailure(Player* bot, uint32_t questId,
            QuestFailureKind kind, const char* context);
        bool TrySetTownRunGoal();
        void EvaluateGoals();
        void SetGoal(BotGoal newGoal);
        void SetAction(std::unique_ptr<Actions::BotAction> newAction);
        ActionRequest BuildActionRequest() const;
        std::vector<DangerArea> GetTravelDangerAreas() const;

        ObjectGuid _botGuid;
        MovementManager* _movement;
        Blackboard::BotBlackboard _blackboard;
        Helper::StuckDetector _stuckDetector;
        Factory::BehaviorProfile _profile;
        Factory::BehaviorTuning _tuning;

        GoalTier _activeTier = GoalTier::Fallback;
        TransitionMetrics _transitionMetrics;
        BotGoal _goal;
        std::unique_ptr<Actions::BotAction> _activeAction;
        std::string _lastActionName;
        std::string _lastActionOutcome;
        std::string _lastActionOutcomeReason;
        uint64 _activeActionPathAttemptGeneration = 0;
        uint64 _activeActionInstanceId = 0;
        uint64 _actionInstanceSequence = 0;
        uint32_t _activeActionElapsedMs = 0;
        uint32_t _activeActionIdleMs = 0;

        SuppressionRegistry _suppressions;
        DeathRecoveryPolicy _deathRecovery;
        QuestProgressWatchdog _progressWatchdog;
        QuestFailureMemory _questFailures;
        HostileContextTracker _hostileContext;
        TravelHazardPolicy _travelHazards;
        NavigationRecoveryPolicy _navigationRecovery;
        FleeRecoveryPolicy _fleeRecovery;
        ProgressionRecoveryPolicy _progressionRecovery;
        TownServiceRecoveryPolicy _townServiceRecovery;
        Helper::RestRecoveryBackoffPolicy _restRecoveryBackoff;
        Helper::WanderRecoveryBackoffPolicy _wanderRecoveryBackoff;

        struct QuestStruggleState
        {
            uint32_t deaths = 0;
            uint32_t flees = 0;
            uint32_t lastStruggleSec = 0;
        };
        std::unordered_map<uint32_t, QuestStruggleState> _questStruggles;

        uint32_t _teleportTimerMs = 0;
        uint32_t _activeQuestId = 0;
        uint32_t _recoveryPauseMs = 0;
        uint32_t _externalControlLogTimerMs = 0;
        uint32_t _inventoryCleanupRetryAfterSec = 0;
        uint32_t _inventoryCleanupBlockedFreeSlots = 0;
        uint32_t _townServiceRetryAfterSec = 0;
        uint32_t _restockRetryAfterSec = 0;
        uint32_t _grindRetryAfterSec = 0;
        uint32_t _conservativeGrindUntilLevel = 0;
        uint32_t _partyDeathWaitMs = 0;
        uint32_t _laggingMemberWaitMs = 0;
        uint32_t _lastSuppressionPruneSec = 0;
        bool _navigationRecoveryPending = false;
        bool _progressionRecoveryPending = false;
        bool _townServiceRecoveryPending = false;
        bool _combatStallRecoveryPending = false;
        bool _fleeRecoveryPending = false;
        ObjectGuid _fleeThreatGuid;
        int32_t _combatInitiationMaxLevelOffset = 0;
    };
}
