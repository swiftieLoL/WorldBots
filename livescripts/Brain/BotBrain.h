#pragma once

#include "BotGoal.h"
#include "ActionRequest.h"
#include "Actions/BotAction.h"
#include "Blackboard/BotBlackboard.h"
#include "Helper/MovementManager.h"
#include "Helper/StuckDetector.h"
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

        BotGoal GetGoal() const { return _goal; }
        std::string GetGoalString() const;
        std::string GetActionString() const;
        const char* GetBehaviorProfileName() const { return Factory::BehaviorProfileName(_profile); }
        uint32_t GetActiveQuestId() const { return _activeQuestId; }
        uint32_t GetQuestProgressWatchId() const { return _progressWatchQuestId; }
        uint32_t GetQuestProgressWatchElapsedMs() const { return _progressWatchNoChangeMs; }
        uint32_t GetGrindUntilLevel() const { return _grindUntilLevel; }
        uint32_t GetInventoryCleanupBlockedFreeSlots() const { return _inventoryCleanupBlockedFreeSlots; }
        uint32_t GetInventoryCleanupRetryRemainingSeconds() const;
        uint32_t GetQuestSuppressionRemainingSeconds(uint32_t questId) const;
        uint32_t GetNpcSuppressionRemainingSeconds(uint32_t npcEntry) const;
        std::vector<std::pair<uint32_t, uint32_t>> GetSuppressedQuests() const;
        std::string GetActionOutcomeReason() const
        {
            return _activeAction ? _activeAction->GetOutcomeReason() : std::string{};
        }

        Player* GetBot() const;
        const Blackboard::BotBlackboard& GetBlackboard() const { return _blackboard; }
        Town::Plan PreviewTownPlan() const;

    private:
        Player* ResolveBot() const;
        void EvaluateGoals();
        void SetGoal(BotGoal newGoal);
        void SetAction(std::unique_ptr<Actions::BotAction> newAction);
        ActionRequest BuildActionRequest() const;

        ObjectGuid _botGuid;
        MovementManager* _movement;
        Blackboard::BotBlackboard _blackboard;
        Helper::StuckDetector _stuckDetector;
        Factory::BehaviorProfile _profile;
        Factory::BehaviorTuning _tuning;

        BotGoal _goal;
        std::unique_ptr<Actions::BotAction> _activeAction;

        uint32_t _teleportTimerMs = 0;
        uint32_t _deadlyQuestId = 0;
        uint32_t _activeQuestId = 0;
        uint32_t _recoveryPauseMs = 0;
        uint32_t _externalControlLogTimerMs = 0;
        uint32_t _progressWatchQuestId = 0;
        uint32_t _progressWatchNoChangeMs = 0;
        uint64_t _progressWatchSignature = 0;
        uint32_t _inventoryCleanupRetryAfterSec = 0;
        uint32_t _inventoryCleanupBlockedFreeSlots = 0;
        uint8_t _lastLearnedLevel = 0;
        uint32_t _grindUntilLevel = 0;
        std::unordered_map<uint32_t, uint32_t> _blacklistedQuests;
        std::unordered_map<uint32_t, uint32_t> _blacklistedNpcs;
        std::vector<uint32_t> _deathTimestamps;
    };
}
