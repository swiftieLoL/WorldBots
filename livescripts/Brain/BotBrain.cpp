#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "BotBrain.h"
#include "Brain/FailurePolicy.h"
#include "Brain/GoalPolicy.h"
#include "Brain/GoalTier.h"
#include "Brain/QuestExclusionPolicy.h"
#include "Brain/QuestSelector.h"
#include "Brain/ThreatAssessmentPolicy.h"
#include "Combat/CombatEngagementPolicy.h"
#include "Actions/IdleAction.h"
#include "Actions/MoveToAction.h"
#include "Actions/WanderAction.h"
#include "Actions/CombatAction.h"
#include "Actions/GrindAction.h"
#include "Actions/LootAction.h"
#include "Actions/VendorAction.h"
#include "Actions/RestAction.h"
#include "Actions/ResurrectAction.h"
#include "Actions/QuestAction.h"
#include "Actions/UnstuckAction.h"
#include "Actions/ActionFactory.h"
#include "Cache/BotCache.h"
#include "Sense/SenseCoordinator.h"
#include "Helper/NpcFinder.h"
#include "Helper/QuestUtils.h"
#include "Helper/EvasionUtils.h"
#include "Helper/CombatUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Helper/ProgressionPolicy.h"
#include "Helper/ProgressionUtils.h"
#include "Helper/RecoveryHubPolicy.h"
#include "Helper/TimeUtils.h"
#include "Party/PartyCoordination.h"
#include "Party/PartyRecruitmentPolicy.h"
#include "Config/BotConfig.h"
#include "Town/TownPlanning.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/SoakDigest.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Entities/Creature/GossipDef.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "Bag.h"
#include "World.h"
#include "Log.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>

namespace Brain
{
    static const char* BotGoalToString(BotGoal goal)
    {
        switch (goal)
        {
            case BotGoal::Idle: return "Idle";
            case BotGoal::Wander: return "Wander";
            case BotGoal::MoveToNpc: return "MoveToNpc";
            case BotGoal::FollowTarget: return "FollowTarget";
            case BotGoal::Combat: return "Combat";
            case BotGoal::Flee: return "Flee";
            case BotGoal::AcceptQuest: return "AcceptQuest";
            case BotGoal::TurnInQuest: return "TurnInQuest";
            case BotGoal::ProgressQuest: return "ProgressQuest";
            case BotGoal::Loot: return "Loot";
            case BotGoal::Vendor: return "Vendor";
            case BotGoal::Rest: return "Rest";
            case BotGoal::Resurrect: return "Resurrect";
            case BotGoal::Unstuck: return "Unstuck";
            case BotGoal::TownRun: return "TownRun";
            case BotGoal::Grind: return "Grind";
            case BotGoal::RevivePartyMember: return "RevivePartyMember";
            case BotGoal::WaitForPartyResurrection: return "WaitForPartyResurrection";
            default: return "Unknown";
        }
    }

    static const char* ActionOutcomeToString(Actions::ActionOutcome outcome)
    {
        switch (outcome)
        {
            case Actions::ActionOutcome::Running: return "Running";
            case Actions::ActionOutcome::Succeeded: return "Succeeded";
            case Actions::ActionOutcome::RetryableFailure: return "RetryableFailure";
            case Actions::ActionOutcome::Blocked: return "Blocked";
            case Actions::ActionOutcome::Unsupported: return "Unsupported";
            case Actions::ActionOutcome::Interrupted: return "Interrupted";
            default: return "Unknown";
        }
    }

    static const char* FailureCategoryToString(Actions::FailureCategory category)
    {
        switch (category)
        {
            case Actions::FailureCategory::None: return "None";
            case Actions::FailureCategory::Transient: return "Transient";
            case Actions::FailureCategory::Stalled: return "Stalled";
            case Actions::FailureCategory::Navigation: return "Navigation";
            case Actions::FailureCategory::Interaction: return "Interaction";
            case Actions::FailureCategory::InventoryCapacity: return "InventoryCapacity";
            case Actions::FailureCategory::ServiceCapability: return "ServiceCapability";
            case Actions::FailureCategory::ContentUnsupported: return "ContentUnsupported";
            case Actions::FailureCategory::ProgressionDifficulty: return "ProgressionDifficulty";
            default: return "Unknown";
        }
    }

    static const char* RecoveryDirectiveToString(Actions::RecoveryDirective directive)
    {
        switch (directive)
        {
            case Actions::RecoveryDirective::None: return "None";
            case Actions::RecoveryDirective::RetryLater: return "RetryLater";
            case Actions::RecoveryDirective::Replan: return "Replan";
            case Actions::RecoveryDirective::GrindUntilLevel: return "GrindUntilLevel";
            default: return "Unknown";
        }
    }

    static void AddMovementEvidence(Diagnostics::StructuredEvent& event,
        const MovementManager* movement)
    {
        if (!movement)
            return;
        event.requestX = movement->GetDestinationX();
        event.requestY = movement->GetDestinationY();
        event.requestZ = movement->GetDestinationZ();
        event.endpointAvailable = movement->GetLastPathAttemptEndpoint(
            event.endpointX, event.endpointY, event.endpointZ);
        event.pathFailure = movement->GetLastPathFailureName();
        event.pathFlags = movement->GetLastPathFlags();
        event.pathAttemptGeneration = movement->GetPathAttemptGeneration();
        event.originFailureCount = movement->GetOriginPathFailureCount();
        event.originDestinationCount =
            movement->GetOriginPathFailureDestinationCount();
        event.originRecoveryRequired = movement->NeedsOriginPathRecovery();
    }

    static bool IsCombatActionTargetAllowed(Player* bot, ObjectGuid targetGuid,
        int32_t maxLevelOffset,
        const SuppressionRegistry& suppressions)
    {
        if (!bot || !targetGuid)
            return false;

        Unit* target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!target || !target->IsAlive() || !target->IsInWorld() ||
            target->GetMap() != bot->GetMap())
            return false;

        Creature* creature = target->ToCreature();
        if (!creature)
            return true;

        CombatInitiationInput input;
        input.botLevel = bot->GetLevel();
        input.targetLevel = creature->GetLevel();
        input.maxLevelOffset = maxLevelOffset;
        input.targetEngagedWithBot = creature->GetVictim() == bot ||
            creature->IsInCombatWith(bot);
        input.targetExecutable =
            Helper::CombatUtils::ValidateTarget(bot, creature) ==
                Helper::CombatUtils::TargetValidationResult::Valid;
        if (!input.targetEngagedWithBot &&
            suppressions.IsCombatTargetSuppressed(targetGuid.GetRawValue()))
        {
            return false;
        }
        return ShouldInitiateCombat(input);
    }

    static ObjectGuid SelectCombatActionTarget(Player* bot,
        const Blackboard::BotBlackboard& blackboard, int32_t maxLevelOffset,
        const SuppressionRegistry& suppressions)
    {
        ObjectGuid targetGuid = blackboard.combat.primaryAttackerGuid;
        if (IsCombatActionTargetAllowed(bot, targetGuid, maxLevelOffset,
            suppressions))
            return targetGuid;

        targetGuid = blackboard.party.groupTargetGuid;
        if (IsCombatActionTargetAllowed(bot, targetGuid, maxLevelOffset,
            suppressions))
            return targetGuid;

        targetGuid = blackboard.combat.currentTargetGuid;
        return IsCombatActionTargetAllowed(bot, targetGuid, maxLevelOffset,
            suppressions)
            ? targetGuid : ObjectGuid::Empty;
    }

    static bool FindVendorForInventory(Player* bot, uint32& vendorEntry,
        bool requireVendor, bool requireRepair, Cache::PositionInfo* resolvedPosition = nullptr,
        const std::unordered_map<uint32_t, uint32_t>* suppressedNpcEntries = nullptr,
        float maxTravelDistance = std::numeric_limits<float>::max())
    {
        vendorEntry = 0;
        if (!bot || !bot->IsInWorld())
            return false;

        auto entryAllowed = [suppressedNpcEntries](uint32_t entry) {
            if (!suppressedNpcEntries)
                return true;
            auto it = suppressedNpcEntries->find(entry);
            return it == suppressedNpcEntries->end() || Helper::MonotonicSeconds() >= it->second;
        };
        auto supplyCapability = [&entryAllowed](Creature* creature) {
            return creature && entryAllowed(creature->GetEntry());
        };
        if (Creature* liveService = Helper::NpcUtils::FindNearbyServiceNpc(
            bot, requireVendor, requireRepair, Constants::TacticalScanRadius, supplyCapability))
        {
            vendorEntry = liveService->GetEntry();
            if (resolvedPosition)
            {
                resolvedPosition->mapId = liveService->GetMapId();
                resolvedPosition->x = liveService->GetPositionX();
                resolvedPosition->y = liveService->GetPositionY();
                resolvedPosition->z = liveService->GetPositionZ();
            }
            return true;
        }

        Cache::PositionInfo vendorPosition;
        bool found = Cache::BotCache::FindNearestVendor(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            requireVendor, requireRepair, vendorEntry, vendorPosition,
            [&entryAllowed](uint32_t entry) {
                return entryAllowed(entry);
            });
        if (found && resolvedPosition)
            *resolvedPosition = vendorPosition;
        if (found && Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
            vendorPosition.x, vendorPosition.y) > maxTravelDistance)
        {
            vendorEntry = 0;
            return false;
        }
        return found;
    }

    static uint64_t CalculateQuestProgressSignature(const Blackboard::ActiveQuest& quest)
    {
        uint64_t signature = 1469598103934665603ULL;
        auto mix = [&signature](uint64_t value) {
            signature ^= value;
            signature *= 1099511628211ULL;
        };
        mix(quest.questId);
        for (const auto& objective : quest.objectives)
        {
            mix(static_cast<uint8_t>(objective.type));
            mix(objective.targetEntry);
            mix(objective.itemId);
            mix(objective.currentCount);
            mix(objective.requiredCount);
        }
        return signature;
    }

    static bool HasInventoryBlockedItemObjective(Player* bot,
        const Blackboard::ActiveQuest& quest, uint32_t* blockedItemId = nullptr,
        InventoryResult* blockedResult = nullptr)
    {
        if (!bot)
            return false;

        for (const auto& objective : quest.objectives)
        {
            if (objective.type != Blackboard::QuestObjectiveType::CollectItem ||
                objective.itemId == 0 || objective.currentCount >= objective.requiredCount)
                continue;

            ItemPosCountVec destination;
            InventoryResult result = bot->CanStoreNewItem(
                NULL_BAG, NULL_SLOT, destination, objective.itemId, 1);
            if (result != EQUIP_ERR_INVENTORY_FULL)
                continue;

            if (blockedItemId)
                *blockedItemId = objective.itemId;
            if (blockedResult)
                *blockedResult = result;
            return true;
        }
        return false;
    }

    BotBrain::BotBrain(Player* bot, MovementManager* movement, Factory::BehaviorProfile profile)
        : _movement(movement), _profile(profile), _tuning(Factory::GetBehaviorTuning(profile)),
          _goal(BotGoal::Idle)
    {
        if (bot)
            _botGuid = bot->GetGUID();
        _activeAction = std::make_unique<Actions::IdleAction>();
        if (_activeAction && bot && bot->IsInWorld() && !bot->IsBeingTeleported() && _movement)
        {
            _activeActionInstanceId = ++_actionInstanceSequence;
            _movement->SetDiagnosticContext(BotGoalToString(_goal),
                _activeAction->GetName(), _activeActionInstanceId, 0);
            _activeAction->Start(bot, _movement);
        }
    }

    BotBrain::~BotBrain()
    {
        Shutdown();
    }

    std::string BotBrain::GetActionOutcomeReason() const
    {
        if (_activeAction && !_activeAction->GetOutcomeReason().empty())
        {
            return std::string(_activeAction->GetName()) + ": " +
                ActionOutcomeToString(_activeAction->GetOutcome()) + " - " +
                _activeAction->GetOutcomeReason();
        }

        return GetPreviousActionOutcome();
    }

    std::string BotBrain::GetPreviousActionOutcome() const
    {
        if (_lastActionName.empty())
            return {};

        std::string summary = _lastActionName + ": " + _lastActionOutcome;
        if (!_lastActionOutcomeReason.empty())
            summary += " - " + _lastActionOutcomeReason;
        return summary;
    }

    std::string BotBrain::GetActionDiagnosticDetail() const
    {
        const auto* grind = dynamic_cast<const Actions::GrindAction*>(
            _activeAction.get());
        return grind ? grind->GetCandidateDiagnostics() : std::string{};
    }

    void BotBrain::Shutdown()
    {
        Player* liveBot = ResolveBot();
        if (_activeAction && _movement)
        {
            _activeAction->Stop(liveBot, _movement);
            if (liveBot && liveBot->IsNonMeleeSpellCast(false))
                liveBot->InterruptNonMeleeSpells(true);
        }

        _activeAction.reset();
        _movement = nullptr;
    }

    Player* BotBrain::ResolveBot() const
    {
        Player* bot = _botGuid ? ObjectAccessor::FindPlayer(_botGuid) : nullptr;
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return nullptr;
        return bot;
    }

    Player* BotBrain::GetBot() const
    {
        return ResolveBot();
    }

    bool BotBrain::IsBlackboardDecisionReady() const
    {
        if (!_blackboard.initialSnapshotReady)
            return false;
        return _blackboard.self.ageMs <= 2000 &&
            _blackboard.combat.ageMs <= 2000 &&
            _blackboard.party.ageMs <= 5000 &&
            _blackboard.spatial.ageMs <= 5000 &&
            _blackboard.nav.ageMs <= 5000 &&
            _blackboard.inv.ageMs <= 15000 &&
            _blackboard.quest.ageMs <= 15000;
    }

    bool BotBrain::IsCreatureRequiredByActiveQuest(uint32_t creatureEntry) const
    {
        if (creatureEntry == 0)
            return false;
        for (const Blackboard::ActiveQuest& quest : _blackboard.quest.activeQuests)
        {
            for (const Blackboard::QuestObjectiveData& objective : quest.objectives)
            {
                if (objective.targetKind == Blackboard::QuestTargetKind::Creature &&
                    objective.targetEntry == creatureEntry &&
                    objective.currentCount < objective.requiredCount)
                    return true;
            }
            if (quest.targetKind == Blackboard::QuestTargetKind::Creature &&
                quest.targetNpcEntry == creatureEntry)
                return true;
        }
        return false;
    }

    uint32_t BotBrain::GetInventoryCleanupRetryRemainingSeconds() const
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        return _inventoryCleanupRetryAfterSec > nowSec ? _inventoryCleanupRetryAfterSec - nowSec : 0;
    }

    uint32_t BotBrain::GetQuestSuppressionRemainingSeconds(uint32_t questId) const
    {
        return _suppressions.GetQuestSuppressionRemaining(questId, Helper::MonotonicSeconds());
    }

    uint32_t BotBrain::GetQuestRetryLevel(uint32_t questId) const
    {
        return _questFailures.GetRetryLevel(questId);
    }

    uint32_t BotBrain::GetQuestFailureCount(uint32_t questId) const
    {
        Player* bot = ResolveBot();
        uint32_t level = bot ? bot->GetLevel() : _blackboard.self.level;
        return _questFailures.GetFailureCount(questId, level);
    }

    bool BotBrain::IsQuestSessionBlocked(uint32_t questId) const
    {
        return _questFailures.IsSessionBlocked(questId);
    }

    uint32_t BotBrain::GetGrindUntilLevel() const
    {
        Player* bot = ResolveBot();
        uint32_t level = bot ? bot->GetLevel() : _blackboard.self.level;
        return _questFailures.GetNextRetryLevel(level);
    }

    uint32_t BotBrain::GetNpcSuppressionRemainingSeconds(uint32_t npcEntry) const
    {
        return _suppressions.GetNpcSuppressionRemaining(npcEntry, Helper::MonotonicSeconds());
    }

    uint32_t BotBrain::GetDeathRecoveryRemainingSeconds() const
    {
        return _deathRecovery.GetRecoveryRemainingSeconds(Helper::MonotonicSeconds());
    }

    std::vector<std::pair<uint32_t, uint32_t>> BotBrain::GetSuppressedQuests() const
    {
        return _suppressions.GetSuppressedQuests(Helper::MonotonicSeconds());
    }

    std::string BotBrain::GetGoalString() const
    {
        return BotGoalToString(_goal);
    }

    std::string BotBrain::GetActionString() const
    {
        if (_activeAction)
            return _activeAction->GetName();
        return "None";
    }

    QuestFailureDecision BotBrain::RememberQuestFailure(Player* bot,
        uint32_t questId, QuestFailureKind kind, const char* context)
    {
        if (!bot || questId == 0)
            return {};

        QuestFailureDecision decision = _questFailures.RecordFailure(
            questId, bot->GetLevel(), kind);
        if (decision.newlyEscalated)
        {
            if (decision.sessionBlocked)
            {
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' marked quest {} unsupported for this session after {}: it will not be selected again",
                        bot->GetName(), questId, context ? context : "a content failure");
                }
            }
            else if (decision.retryAtLevel > bot->GetLevel())
            {
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' deferred quest {} until level {} after {} repeated failure(s) at level {}: {}",
                        bot->GetName(), questId, decision.retryAtLevel,
                        decision.failureCount, bot->GetLevel(),
                        context ? context : "progression failure");
                }
            }
        }
        if (decision.sessionBlocked || decision.retryAtLevel > bot->GetLevel())
        {
            if (_activeQuestId == questId)
                _activeQuestId = 0;
        }
        return decision;
    }

    void BotBrain::Sense(uint32_t deltaMs)
    {
        Player* bot = ResolveBot();
        if (!bot) return;
        auto excludedQuestIds = _questFailures.GetDeferredQuestIds(bot->GetLevel());
        uint32_t nowSec = Helper::MonotonicSeconds();
        for (const auto& [questId, expirySec] : _suppressions.GetBlacklistedQuests())
        {
            if (expirySec > nowSec)
                excludedQuestIds.insert(questId);
        }
        _blackboard.quest.excludedQuestIds = std::move(excludedQuestIds);
        _blackboard.quest.excludedQuestDestinations =
            _suppressions.GetQuestDestinationSuppressions();
        Sense::SenseCoordinator::UpdateAll(bot, _movement, _blackboard, deltaMs);
    }

    void BotBrain::EvaluateGoals()
    {
        Player* bot = ResolveBot();
        if (!bot)
        {
            SetGoal(BotGoal::Idle);
            return;
        }

        // Clean up typed suppression entries. Quest IDs and NPC entries occupy
        // different namespaces and must never accidentally suppress each other.
        uint32_t nowSec = Helper::MonotonicSeconds();
        if (nowSec != _lastSuppressionPruneSec)
        {
            _suppressions.PruneExpired(nowSec);
            _travelHazards.PruneExpired(nowSec);
            _questFailures.PruneOutleveled(bot->GetLevel());
            _lastSuppressionPruneSec = nowSec;
        }
        if (_progressionRecovery.ObserveProgress(
            nowSec, bot->GetLevel(), bot->GetXP()))
        {
            Actions::UnstuckAction::RecordProgress(bot->GetGUID());
            _wanderRecoveryBackoff.RecordProgress();
        }

        if (_conservativeGrindUntilLevel != 0 &&
            bot->GetLevel() >= _conservativeGrindUntilLevel)
        {
            if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' reached level {}; restoring the configured grind difficulty band",
                    bot->GetName(), bot->GetLevel());
            }
            _conservativeGrindUntilLevel = 0;
        }

        bool deathRecoveryActive = _deathRecovery.IsRecoveryActive(nowSec);
        float routineVendorTravelDistance = Config::BotConfig::GetMaxRoutineVendorTravelDistance();
        float maxServiceTravelDistance = deathRecoveryActive
            ? std::min(120.0f, routineVendorTravelDistance) : routineVendorTravelDistance;

        // Preserve the active hostile spawn across Grind, Combat, and Flee.
        // If the bot subsequently dies, this is the exact non-quest spawn
        // that future grind selection should avoid.
        _hostileContext.RememberHostileTarget(bot, _activeAction.get(), _goal);
        _hostileContext.RememberProactiveRouteTarget(_activeAction.get(), _goal);

        // A failed capacity cleanup describes the inventory, not the vendor.
        // Retry after a bounded pause, or immediately if capacity has
        // materially improved in the meantime.
        if (_inventoryCleanupRetryAfterSec != 0 &&
            (nowSec >= _inventoryCleanupRetryAfterSec ||
             _blackboard.inv.freeBagSlots > _inventoryCleanupBlockedFreeSlots))
        {
            _inventoryCleanupRetryAfterSec = 0;
            _inventoryCleanupBlockedFreeSlots = 0;
        }
        bool inventoryCleanupDeferred = _inventoryCleanupRetryAfterSec != 0;
        if (_townServiceRetryAfterSec != 0 &&
            nowSec >= _townServiceRetryAfterSec)
        {
            _townServiceRetryAfterSec = 0;
        }
        bool townServiceDeferred = _townServiceRetryAfterSec != 0;
        if (_grindRetryAfterSec != 0 && nowSec >= _grindRetryAfterSec)
        {
            Diagnostics::StructuredEvent event;
            event.event = "grind_backoff_expired";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction ? _activeAction->GetName() : "";
            event.actionInstance = _activeActionInstanceId;
            event.questId = _activeQuestId;
            AddMovementEvidence(event, _movement);
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            _grindRetryAfterSec = 0;
        }
        bool grindFallbackReady = Config::BotConfig::IsGrindFallbackEnabled() &&
            _grindRetryAfterSec == 0;
        if (_restRecoveryBackoff.Expire(nowSec))
        {
            Diagnostics::StructuredEvent event;
            event.event = "rest_backoff_expired";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction ? _activeAction->GetName() : "";
            event.actionInstance = _activeActionInstanceId;
            AddMovementEvidence(event, _movement);
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
        }
        if (_wanderRecoveryBackoff.Expire(nowSec))
        {
            Diagnostics::StructuredEvent event;
            event.event = "wander_backoff_expired";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction ? _activeAction->GetName() : "";
            event.actionInstance = _activeActionInstanceId;
            AddMovementEvidence(event, _movement);
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
        }
        bool wanderFallbackReady = _wanderRecoveryBackoff.IsReady();

        // Priority 0: Ghost & Graveyard Resurrect State (Tier 0: Survival)
        if (!bot->IsAlive())
        {
            _activeTier = GoalTier::Survival;
            // Managed bots accept a real resurrection request generated by a
            // teammate's spell. This preserves normal mana, range, cast-time,
            // and cooldown rules while avoiding a client confirmation dialog.
            if (bot->IsResurrectRequested())
            {
                bot->ResurrectUsingRequestData();
                _partyDeathWaitMs = 0;
                _blackboard.self.isDead = false;
                _blackboard.self.health = bot->GetHealth();
                _blackboard.self.healthPct = bot->GetHealthPct();
                SetGoal(BotGoal::Idle);
                return;
            }

            // Give the designated party resurrector enough time to complete a
            // normal 10-second resurrection cast before using the existing
            // graveyard fallback.
            if (_blackboard.party.designatedResurrectorGuid &&
                _blackboard.party.designatedResurrectorGuid != bot->GetGUID() &&
                _partyDeathWaitMs < 25000)
            {
                SetGoal(BotGoal::WaitForPartyResurrection);
                return;
            }

            if (_goal != BotGoal::Resurrect)
            {
                constexpr float FatalAreaSuppressionRadius = 80.0f;
                constexpr uint32_t FatalAreaSuppressionSeconds = 900;
                _suppressions.SuppressDangerArea(bot->GetMapId(), bot->GetPositionX(),
                    bot->GetPositionY(), FatalAreaSuppressionRadius,
                    nowSec + FatalAreaSuppressionSeconds);
                // Loot discovery owns a separate per-bot spatial index. Feed
                // it the same fatal location so a nearby corpse cannot bypass
                // the hunting exclusion and pull the bot straight back into
                // the area that just killed it.
                Actions::LootAction::SuppressDangerousArea(bot, ObjectGuid::Empty,
                    FatalAreaSuppressionRadius, FatalAreaSuppressionSeconds);
                _hostileContext.SuppressFatalHostileTarget(bot, _suppressions, [this](uint32_t entry) {
                    return IsCreatureRequiredByActiveQuest(entry);
                });
                uint32_t failedRouteQuestId =
                    _hostileContext.SuppressFatalProactiveRoute(bot, _suppressions);
                uint32_t failedQuestId = failedRouteQuestId;
                if (failedQuestId == 0 && _goal == BotGoal::ProgressQuest &&
                    _activeAction)
                    failedQuestId = _activeAction->GetRelatedQuestId();
                if (failedQuestId != 0)
                {
                    auto& struggle = _questStruggles[failedQuestId];
                    struggle.deaths++;
                    struggle.lastStruggleSec = nowSec;

                    uint32_t recruited = 0;
                    Group* group = bot->GetGroup();
                    bool canRecruit = (!group || (group->IsLeader(bot->GetGUID()) && group->GetMembersCount() < Party::MaxGroupSize));
                    if (canRecruit)
                    {
                        recruited = Party::PartyRecruitmentPolicy::TryRecruitForQuest(bot, failedQuestId);
                    }

                    bool hasPartyHelp = (recruited > 0) || (group && group->GetMembersCount() > 1 && struggle.deaths < 3);
                    if (hasPartyHelp)
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' died on quest {} but has party assistance (Recruited: {}, Group size: {}); continuing quest without blacklisting",
                            bot->GetName(), failedQuestId, recruited, bot->GetGroup() ? bot->GetGroup()->GetMembersCount() : 1);
                        Diagnostics::StructuredEvent event;
                        event.event = "party_formed_for_quest";
                        event.goal = BotGoalToString(_goal);
                        event.action = _activeAction ? _activeAction->GetName() : "";
                        event.questId = failedQuestId;
                        event.details = "recruited=" + std::to_string(recruited) + ";group_size=" + std::to_string(bot->GetGroup() ? bot->GetGroup()->GetMembersCount() : 1);
                        Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                    }
                    else
                    {
                        constexpr uint32_t FatalQuestSuppressionSeconds = 900;
                        _suppressions.SuppressQuest(failedQuestId,
                            nowSec + FatalQuestSuppressionSeconds);
                        RememberQuestFailure(bot, failedQuestId,
                            QuestFailureKind::UnsafeRoute,
                            failedRouteQuestId != 0
                                ? "unsafe world travel"
                                : "death during quest execution");
                        if (failedRouteQuestId == 0)
                        {
                            TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' died while executing quest {}; deferring it for {} seconds and reconsidering it after repeated deaths",
                                bot->GetName(), failedQuestId,
                                FatalQuestSuppressionSeconds);
                        }
                    }
                }
                _deathRecovery.RecordDeath(nowSec);
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::Deaths);

                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' died! (Deaths in last 5 minutes: {})",
                        bot->GetName(), _deathRecovery.GetRecentDeathCount());
                }

                if (_deathRecovery.ShouldTriggerCircuitBreaker(nowSec))
                {
                    _deathRecovery.SetDeadlyQuestId(0);
                    _deathRecovery.ActivateCircuitBreaker(nowSec);
                    _conservativeGrindUntilLevel = std::max<uint32_t>(
                        _conservativeGrindUntilLevel,
                        Helper::NextProgressionRetryLevel(bot->GetLevel()));
                    Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::CircuitBreakerFired);
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' died {} times within 5 minutes; returning home, repairing fully, and pausing proactive travel and combat for {} minutes",
                        bot->GetName(), DeathRecoveryPolicy::DeathCircuitThreshold, DeathRecoveryPolicy::DeathRecoverySeconds / 60);

                    _teleportTimerMs = 2500;
                    SetGoal(BotGoal::Unstuck);
                    return;
                }
            }

            SetGoal(BotGoal::Resurrect);
            return;
        }

        // Destination-specific failures are deliberately not treated as an
        // off-navmesh origin. If failures span quests, town work, and Grind,
        // however, a complete no-XP interval is stronger procedural evidence
        // that the current progression ecology is not useful. Recover only at
        // a safe interruption point and never at the configured level cap.
        if (!_progressionRecoveryPending && !bot->IsInCombat() &&
            _goal != BotGoal::Unstuck &&
            bot->GetLevel() < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) &&
            _progressionRecovery.ShouldRecoverFromStall(nowSec))
        {
            _progressionRecoveryPending = true;
            Diagnostics::StructuredEvent event;
            event.event = "progression_stall_relocation_triggered";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction ? _activeAction->GetName() : "";
            event.actionInstance = _activeActionInstanceId;
            event.questId = _activeQuestId;
            event.recoveryDirective = "Relocate";
            AddMovementEvidence(event, _movement);
            std::ostringstream details;
            details << "progress_age_s="
                << _progressionRecovery.GetProgressAge(nowSec)
                << ";stall_threshold_s="
                << ProgressionRecoveryPolicy::StallThresholdSeconds
                << ";stall_recovery_count="
                << _progressionRecovery.GetStallRecoveryCount()
                << ";stall_recovery_limit="
                << ProgressionRecoveryPolicy::MaxStallRecoveriesWithoutProgress
                << ";recovery_cooldown_s="
                << _progressionRecovery.GetStallRecoveryCooldownRemaining(nowSec);
            event.details = details.str();
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' gained no XP for {} minutes while cycling progression work; relocating to a procedurally selected level-safe ecology (attempt {}/{})",
                bot->GetName(),
                ProgressionRecoveryPolicy::StallThresholdSeconds / 60,
                _progressionRecovery.GetStallRecoveryCount(),
                ProgressionRecoveryPolicy::MaxStallRecoveriesWithoutProgress);
            SetGoal(BotGoal::Unstuck);
            return;
        }

        // Failed path requests never create an active movement lease, so the
        // ordinary stuck detector cannot see an origin that is detached from
        // the navmesh. Escalate live, target-independent evidence before an
        // action's generic idle timeout erases the path failure category.
        if (!_navigationRecoveryPending && _movement &&
            _movement->NeedsOriginPathRecovery() && !bot->IsInCombat() &&
            _goal != BotGoal::Unstuck)
        {
            _navigationRecoveryPending = true;
            Diagnostics::StructuredEvent event;
            event.event = "origin_recovery_requested";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction ? _activeAction->GetName() : "";
            event.actionInstance = _activeActionInstanceId;
            event.questId = _activeQuestId;
            AddMovementEvidence(event, _movement);
            event.details = "stable origin repeatedly failed to produce viable local navmesh corridors";
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            Diagnostics::SoakDigest::Record(
                static_cast<uint32_t>(bot->GetGUID().GetCounter()),
                Diagnostics::SoakEvent::StuckEscalations);
            TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' could not produce a viable corridor from its stable origin ({:.1f}, {:.1f}, {:.1f}) after {} rejected requests across {} destination(s) (flags {}); starting navmesh recovery",
                bot->GetName(), bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), _movement->GetOriginPathFailureCount(),
                _movement->GetOriginPathFailureDestinationCount(),
                _movement->GetLastPathFlags());
        }

        // A bot below the map (or on another disconnected navmesh island)
        // cannot start a path, so the ordinary moving/stationary detector can
        // never observe it as stuck. Preserve the emergency goal until its
        // relocation action has run.
        if (Helper::RecoveryHubPolicy::HasPendingRecovery(
            _navigationRecoveryPending, _progressionRecoveryPending,
            _townServiceRecoveryPending, _combatStallRecoveryPending,
            _fleeRecoveryPending))
        {
            _activeTier = GoalTier::Survival;
            SetGoal(BotGoal::Unstuck);
            return;
        }

        // Tier 1: Tactical
        _activeTier = GoalTier::Tactical;
        bool questThreatContext = _goal == BotGoal::AcceptQuest ||
            _goal == BotGoal::TurnInQuest || _goal == BotGoal::ProgressQuest;
        int32_t ambientThreatMaxLevelOffset = SelectAmbientThreatMaxLevelOffset(
            questThreatContext, Config::BotConfig::GetGrindMaxLevelOffset(),
            Config::BotConfig::GetQuestMaxLevelsAboveBot());
        _combatInitiationMaxLevelOffset = ambientThreatMaxLevelOffset;
        bool isLowVitals = _blackboard.self.healthPct <= _tuning.restHealthPct ||
            (_blackboard.self.manaPct <= _tuning.restManaPct && bot->GetMaxPower(POWER_MANA) > 0);
        bool isRecovering = (_goal == BotGoal::Rest) && (_blackboard.self.healthPct < 85 || (_blackboard.self.manaPct < 85 && bot->GetMaxPower(POWER_MANA) > 0));
        bool vitalsRecovered = _blackboard.self.healthPct >= 85 &&
            (bot->GetMaxPower(POWER_MANA) == 0 || _blackboard.self.manaPct >= 85);
        if (vitalsRecovered)
            _restRecoveryBackoff.RecordRecovery();

        ImmediateGoalInput immediateInput;
        immediateInput.currentGoal = _goal;
        immediateInput.inCombat = _blackboard.self.inCombat;
        ObjectGuid combatActionTarget = deathRecoveryActive
            ? ObjectGuid::Empty : SelectCombatActionTarget(bot, _blackboard,
                ambientThreatMaxLevelOffset, _suppressions);
        immediateInput.hasCombatTarget = !_blackboard.combat.attackerGuids.empty() ||
            !combatActionTarget.IsEmpty();
        immediateInput.hasPartyTarget = !deathRecoveryActive &&
            combatActionTarget &&
            combatActionTarget == _blackboard.party.groupTargetGuid;
        immediateInput.hasCombatActionTarget = !combatActionTarget.IsEmpty();
        immediateInput.preserveFlee = !deathRecoveryActive && _goal == BotGoal::Flee &&
            _activeAction && !_activeAction->IsComplete();
        immediateInput.preserveCombat = false;
        if (!deathRecoveryActive && _goal == BotGoal::Combat && _activeAction &&
            !_activeAction->IsComplete())
        {
            ObjectGuid activeTargetGuid = _activeAction->GetRelatedTargetGuid();
            Unit* activeTarget = activeTargetGuid
                ? ObjectAccessor::GetUnit(*bot, activeTargetGuid) : nullptr;
            immediateInput.preserveCombat = activeTarget && activeTarget->IsInWorld() &&
                activeTarget->IsAlive() && activeTarget->GetMap() == bot->GetMap();
            if (immediateInput.preserveCombat)
                immediateInput.hasCombatActionTarget = true;
        }
        immediateInput.preserveProgressQuest = !deathRecoveryActive && _goal == BotGoal::ProgressQuest &&
            _activeAction && !_activeAction->IsComplete();
        immediateInput.preserveGrind = !deathRecoveryActive && _goal == BotGoal::Grind &&
            _activeAction && !_activeAction->IsComplete();
        immediateInput.preserveNonInterruptible = _activeAction &&
            !_activeAction->IsComplete() && !_activeAction->IsInterruptible();
        ObjectGuid overwhelmingThreatGuid;
        uint32_t overwhelmingThreatEntry = 0;
        bool aboveLevelThreat = false;
        bool outnumberedThreat = false;
        bool overwhelmingThreatEngaged = false;
        ObjectGuid firstActiveAttackerGuid;
        uint32_t activeAttackerCount = 0;
        uint32_t highestAttackerLevel = 0;
        for (ObjectGuid attackerGuid : _blackboard.combat.attackerGuids)
        {
            Unit* attacker = ObjectAccessor::GetUnit(*bot, attackerGuid);
            if (!attacker || !attacker->IsAlive() || attacker->GetMap() != bot->GetMap())
                continue;
            if (!firstActiveAttackerGuid)
                firstActiveAttackerGuid = attackerGuid;
            ++activeAttackerCount;
            highestAttackerLevel = std::max<uint32_t>(highestAttackerLevel, attacker->GetLevel());
        }
        for (ObjectGuid hostileGuid : _blackboard.spatial.hostileGuids)
        {
            Unit* hostile = ObjectAccessor::GetUnit(*bot, hostileGuid);
            if (!hostile || !hostile->IsAlive() || !hostile->IsInWorld() || hostile->GetMap() != bot->GetMap())
                continue;
            bool engaged = hostile->GetVictim() == bot || hostile->IsInCombatWith(bot);
            bool canAggroNow = engaged;
            if (!canAggroNow)
            {
                if (Creature* c = hostile->ToCreature())
                    canAggroNow = c->CanStartAttack(bot, false);
                else
                    canAggroNow = hostile->IsHostileTo(bot);
            }
            AboveLevelAttackerRiskInput aboveLevelRisk;
            aboveLevelRisk.botLevel = bot->GetLevel();
            aboveLevelRisk.attackerLevel = hostile->GetLevel();
            aboveLevelRisk.healthPct = _blackboard.self.healthPct;
            aboveLevelRisk.engaged = engaged;
            aboveLevelRisk.playerClass = bot->GetClass();
            if (canAggroNow && Helper::IsCreatureAboveGrindingCeiling(
                bot->GetLevel(), hostile->GetLevel(), ambientThreatMaxLevelOffset) &&
                ShouldFleeFromAboveLevelAttacker(aboveLevelRisk))
            {
                overwhelmingThreatGuid = hostileGuid;
                overwhelmingThreatEntry = hostile->GetEntry();
                aboveLevelThreat = true;
                overwhelmingThreatEngaged = engaged;
                break;
            }
        }
        MultiAttackerRiskInput multiAttackerRisk;
        multiAttackerRisk.botLevel = bot->GetLevel();
        multiAttackerRisk.highestAttackerLevel = highestAttackerLevel;
        multiAttackerRisk.attackerCount = activeAttackerCount;
        multiAttackerRisk.healthPct = _blackboard.self.healthPct;
        multiAttackerRisk.playerClass = bot->GetClass();
        if (!overwhelmingThreatGuid && ShouldFleeFromMultipleAttackers(multiAttackerRisk))
        {
            overwhelmingThreatGuid = combatActionTarget
                ? combatActionTarget : firstActiveAttackerGuid;
            outnumberedThreat = true;
            overwhelmingThreatEngaged = true;
            if (Creature* overwhelmingThreat = ObjectAccessor::GetCreature(
                *bot, overwhelmingThreatGuid))
                overwhelmingThreatEntry = overwhelmingThreat->GetEntry();
        }

        bool lowHealthEscape = _blackboard.self.inCombat &&
            IsCriticalFleeHealth(_blackboard.self.healthPct);
        immediateInput.shouldFlee = ShouldFleeForThreat(lowHealthEscape,
            immediateInput.hasCombatActionTarget,
            !overwhelmingThreatGuid.IsEmpty());
        if (immediateInput.shouldFlee)
        {
            _fleeThreatGuid = overwhelmingThreatGuid
                ? overwhelmingThreatGuid : combatActionTarget;

            if (_goal != BotGoal::Flee)
            {
                const char* reason = lowHealthEscape ? "low health" :
                    (aboveLevelThreat ? "above-level hostile" :
                    (outnumberedThreat ? "combined attacker risk" : "tactical danger"));
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Flee] Bot '{}' escaping due to {} (HP: {}%, Attackers: {}, Highest attacker level: {}, Bot level: {}, Threat entry: {}, Threat engaged: {})",
                        bot->GetName(), reason, _blackboard.self.healthPct,
                        activeAttackerCount, highestAttackerLevel, bot->GetLevel(),
                        overwhelmingThreatEntry, overwhelmingThreatEngaged ? "Yes" : "No");
                }

                if (_activeQuestId != 0)
                {
                    auto& struggle = _questStruggles[_activeQuestId];
                    struggle.flees++;
                    struggle.lastStruggleSec = nowSec;
                    if (struggle.flees >= Party::StruggleFleeThreshold)
                    {
                        Group* group = bot->GetGroup();
                        bool canRecruit = (!group || (group->IsLeader(bot->GetGUID()) && group->GetMembersCount() < Party::MaxGroupSize));
                        if (canRecruit)
                        {
                            uint32_t recruited = Party::PartyRecruitmentPolicy::TryRecruitForQuest(bot, _activeQuestId);
                            if (recruited > 0)
                            {
                                TC_LOG_INFO("server", "[WorldBots] [Party] Bot '{}' struggling with flee on quest {}; recruited {} bot(s) to assist (Group size: {})",
                                    bot->GetName(), _activeQuestId, recruited, bot->GetGroup() ? bot->GetGroup()->GetMembersCount() : 1);
                            }
                        }
                    }
                }
            }

            // A proactive route that led into an overwhelming threat should not
            // resume the same destination as soon as the escape leg finishes.
            // Precautionary avoidance of a nearby creature must not blacklist
            // an otherwise viable quest or count toward teleport recovery.
            // Attribute route danger only after the threat actually engages.
            if (ShouldAttributeThreatToProactiveRoute(
                    !overwhelmingThreatGuid.IsEmpty(), overwhelmingThreatEngaged) &&
                _goal != BotGoal::Flee)
            {
                if (_goal == BotGoal::Grind)
                {
                    if (auto* grind = dynamic_cast<Actions::GrindAction*>(_activeAction.get()))
                    {
                        uint32_t destinationEntry = grind->GetHuntingDestinationEntry();
                        if (destinationEntry != 0)
                        {
                            constexpr uint32_t UnsafeHuntingDestinationSuppressionSeconds = 300;
                            _suppressions.SuppressGrindEntry(destinationEntry,
                                nowSec + UnsafeHuntingDestinationSuppressionSeconds);
                            if (Diagnostics::BotTrace::ShouldLog(bot))
                            {
                                TC_LOG_WARN("server", "[WorldBots] [Grind] Bot '{}' encountered an overwhelming threat (Entry {}) near hunting Entry {}; suppressing that hunting destination for {} seconds and escaping",
                                    bot->GetName(), overwhelmingThreatEntry, destinationEntry,
                                    UnsafeHuntingDestinationSuppressionSeconds);
                            }
                        }
                    }
                }
                else if (_hostileContext.HasProactiveRouteTarget())
                {
                    // A route hazard belongs to the corridor, not only to the
                    // quest that happened to discover it. Mark the engaged
                    // hostile's location so every subsequent quest route can
                    // reject or replan ground legs through the same area.
                    constexpr float UnsafeRouteCorridorRadius = 45.0f;
                    constexpr uint32_t UnsafeRouteCorridorSuppressionSeconds = 900;
                    bool distinctHazardsRequireRecovery = false;
                    if (Unit* unsafeThreat = ObjectAccessor::GetUnit(
                        *bot, overwhelmingThreatGuid))
                    {
                        _travelHazards.SuppressDangerArea(unsafeThreat->GetMapId(),
                            unsafeThreat->GetPositionX(), unsafeThreat->GetPositionY(),
                            UnsafeRouteCorridorRadius,
                            nowSec + UnsafeRouteCorridorSuppressionSeconds);
                        distinctHazardsRequireRecovery =
                            _travelHazards.RecordUnsafeRoute(nowSec,
                                unsafeThreat->GetMapId(),
                                unsafeThreat->GetPositionX(),
                                unsafeThreat->GetPositionY());
                        if (Diagnostics::BotTrace::ShouldLog(bot))
                        {
                            TC_LOG_WARN("server", "[WorldBots] [Travel] Bot '{}' personally marked the corridor around hostile Entry {} unsafe within {:.0f} yards for {} seconds; only this bot's travel routes will avoid it",
                                bot->GetName(), overwhelmingThreatEntry,
                                UnsafeRouteCorridorRadius,
                                UnsafeRouteCorridorSuppressionSeconds);
                        }
                    }
                    // The encounter teaches this bot about the place, not
                    // that the quest or service destination is inherently
                    // invalid. A later short-leg plan may safely go around it.
                    _hostileContext.ClearProactiveRoute();
                    if (distinctHazardsRequireRecovery)
                    {
                        _deathRecovery.SetDeadlyQuestId(0);
                        _deathRecovery.ActivateCircuitBreaker(nowSec);
                        _conservativeGrindUntilLevel = std::max<uint32_t>(
                            _conservativeGrindUntilLevel,
                            Helper::NextProgressionRetryLevel(bot->GetLevel()));
                        Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::CircuitBreakerFired);
                        TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' encountered {} unsafe proactive routes within {} minutes; returning home and pausing proactive travel and combat for {} minutes",
                            bot->GetName(), TravelHazardPolicy::HazardRecoveryThreshold,
                            TravelHazardPolicy::HazardWindowSeconds / 60,
                            DeathRecoveryPolicy::DeathRecoverySeconds / 60);
                        _teleportTimerMs = 2500;
                        SetGoal(BotGoal::Unstuck);
                        return;
                    }
                }
                else if (_goal == BotGoal::Loot)
                {
                    ObjectGuid lootTargetGuid = _activeAction
                        ? _activeAction->GetRelatedTargetGuid() : ObjectGuid::Empty;
                    if (lootTargetGuid)
                    {
                        constexpr uint32_t UnsafeLootSuppressionSeconds = 300;
                        constexpr float UnsafeLootAreaRadius = 60.0f;
                        Actions::LootAction::SuppressTarget(
                            bot, lootTargetGuid, UnsafeLootSuppressionSeconds);
                        Actions::LootAction::SuppressDangerousArea(bot, lootTargetGuid,
                            UnsafeLootAreaRadius, UnsafeLootSuppressionSeconds);
                        if (Diagnostics::BotTrace::ShouldLog(bot))
                        {
                            TC_LOG_WARN("server", "[WorldBots] [Loot] Bot '{}' encountered an overwhelming threat (Entry {}) while approaching loot GUID {}; suppressing loot within {:.0f} yards of that target for {} seconds and escaping",
                                bot->GetName(), overwhelmingThreatEntry,
                                lootTargetGuid.GetRawValue(), UnsafeLootAreaRadius,
                                UnsafeLootSuppressionSeconds);
                        }
                    }
                }
            }
        }
        immediateInput.shouldRevivePartyMember = !deathRecoveryActive && !_blackboard.self.inCombat &&
            _blackboard.party.deadGroupMemberGuid &&
            _blackboard.party.designatedResurrectorGuid == bot->GetGUID();
        immediateInput.shouldRest = (isLowVitals || isRecovering) &&
            _restRecoveryBackoff.IsReady() &&
            !_blackboard.self.inCombat && _blackboard.combat.attackerGuids.empty();

        ImmediateGoalDecision immediate = SelectImmediateGoal(immediateInput);
        if (immediate.handled)
        {
            SetGoal(immediate.goal);
            return;
        }

        if (deathRecoveryActive)
        {
            _activeTier = GoalTier::Survival;
            SetGoal(BotGoal::Idle);
            return;
        }

        // Tier 2: Emergency (capacity / loot)
        _activeTier = GoalTier::Emergency;

        // LootAction can discover a full inventory before the periodic
        // inventory scan identifies sellable junk. Route directly to a
        // vendor so the same loot target is not retried in a tight loop.
        bool hasInventoryBlockedLoot = Actions::LootAction::HasInventoryBlockedLoot(bot);
        bool hasQuestItemCapacityBlock = false;
        for (const auto& activeQuest : _blackboard.quest.activeQuests)
        {
            if (!_suppressions.IsQuestSuppressed(activeQuest.questId) &&
                !_questFailures.IsDeferred(activeQuest.questId, bot->GetLevel()) &&
                HasInventoryBlockedItemObjective(bot, activeQuest))
            {
                hasQuestItemCapacityBlock = true;
                break;
            }
        }

        // Do not kill a quest-item source before the required item can fit.
        // Create one slot proactively, rather than waiting for a unique corpse
        // to expose the capacity problem after combat.
        if (hasQuestItemCapacityBlock && !inventoryCleanupDeferred &&
            !townServiceDeferred)
        {
            uint32 vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance) &&
                TrySetTownRunGoal())
            {
                return;
            }
        }

        if (hasInventoryBlockedLoot && !inventoryCleanupDeferred &&
            !townServiceDeferred)
        {
            uint32 vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance) &&
                TrySetTownRunGoal())
            {
                return;
            }
        }

        // A completed quest whose reward cannot fit belongs to VendorAction,
        // not TurnInQuestAction. Do this before selecting a turn-in task so
        // the bot clears capacity first and only approaches the quest giver
        // when RewardQuest can succeed.
        for (const auto& completed : _blackboard.quest.completedQuests)
        {
            if (inventoryCleanupDeferred || townServiceDeferred)
                break;
            if (_blackboard.party.isGroupLeader &&
                completed.questId == _blackboard.party.laggingQuestId)
                continue;
            if (_suppressions.IsQuestSuppressed(completed.questId))
                continue;
            if (_questFailures.IsDeferred(completed.questId, bot->GetLevel()))
                continue;
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
            if (!Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate))
                continue;

            uint32 vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance))
            {
                bool enteringTownRun = _goal != BotGoal::TownRun;
                if (TrySetTownRunGoal())
                {
                    if (enteringTownRun && Diagnostics::BotTrace::ShouldLog(bot))
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' cannot receive the reward for completed quest {} ('{}') with its current inventory; selecting VendorAction before TurnInQuestAction",
                            bot->GetName(), completed.questId, questTemplate->GetTitle());
                    }
                    return;
                }
            }
        }

        // Priority 1.8: Emergency Vendoring if bags are full/nearly full (<= 1 free slot) and bot has items to sell or gear to repair
        if (!inventoryCleanupDeferred && !townServiceDeferred &&
            (_blackboard.inv.bagsFull || _blackboard.inv.freeBagSlots <= 1) &&
            (_blackboard.inv.hasItemsToSell || _blackboard.inv.needsRepair))
        {
            uint32_t vendorEntry = 0;
            bool hasService = _blackboard.inv.hasItemsToSell &&
                FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                    &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);
            if (!hasService && _blackboard.inv.needsRepair)
            {
                vendorEntry = 0;
                hasService = FindVendorForInventory(bot, vendorEntry, false, true, nullptr,
                    &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);
            }
            if (hasService && TrySetTownRunGoal())
            {
                return;
            }
        }

        // Priority 2: Post-combat Looting (Loot nearby corpses BEFORE running across map for quest turn-ins)
        // Do not immediately retry the same corpse while every store attempt
        // is guaranteed to fail. The blocked marker is cleared by
        // HasInventoryBlockedLoot as soon as real bag capacity appears.
        if (Actions::LootAction::HasLootableTargets(bot, {}))
        {
            SetGoal(BotGoal::Loot);
            return;
        }

        // Priority 3: Vendoring before quest turn-in prevents a full/low-space
        // inventory from rejecting a quest reward and then trapping the bot in
        // a non-interruptible turn-in action.
        bool restockDue = !townServiceDeferred &&
            _blackboard.inv.needsRestock &&
            (_restockRetryAfterSec == 0 || nowSec >= _restockRetryAfterSec);
        bool needsRoutineInventoryService = !inventoryCleanupDeferred &&
            !townServiceDeferred &&
            ((_blackboard.inv.lowBagSpace && _blackboard.inv.hasItemsToSell) || _blackboard.inv.needsRepair);
        if (needsRoutineInventoryService || restockDue)
        {
            uint32_t vendorEntry = 0;
            bool hasService = false;
            if (_blackboard.inv.lowBagSpace && _blackboard.inv.hasItemsToSell)
            {
                hasService = FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                    &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);
            }
            if (!hasService && _blackboard.inv.needsRepair)
            {
                vendorEntry = 0;
                hasService = FindVendorForInventory(bot, vendorEntry, false, true, nullptr,
                    &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);
            }
            if (!hasService && restockDue)
            {
                vendorEntry = 0;
                hasService = FindVendorForInventory(bot, vendorEntry, true, false,
                    nullptr, &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);
            }
            if (hasService && TrySetTownRunGoal())
            {
                return;
            }
        }

        // A bot whose recovery supplies could not restore critically low
        // vitals must not leave the bounded Rest backoff by starting proactive
        // combat or travel. Nearby loot and a viable restock visit above remain
        // available; otherwise wait safely until recovery is eligible again.
        if (isLowVitals && !_restRecoveryBackoff.IsReady())
        {
            _activeTier = GoalTier::Survival;
            SetGoal(BotGoal::Idle);
            return;
        }

        // Priority 3.3: Party Leader Town Run Coordination
        // If a party member has full bags (<= 1 free slot) or broken gear,
        // the leader initiates a town run so the entire party travels to a vendor hub together.
        if (_blackboard.party.isGroupLeader && _blackboard.party.memberNeedsTownRun &&
            !townServiceDeferred)
        {
            uint32_t vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance) &&
                TrySetTownRunGoal())
            {
                return;
            }
        }

        // Priority 3.4: the leader keeps a completed shared quest nearby until
        // local party members finish it, so kills and interactions continue
        // to receive group credit before the coordinated turn-in.
        if (_blackboard.party.isGroupLeader && _blackboard.party.laggingQuestMemberGuid &&
            _laggingMemberWaitMs < 600000)
        {
            SetGoal(BotGoal::FollowTarget);
            return;
        }

        // Priority 3.5: Turn in completed quests
        if (!_blackboard.quest.completedQuests.empty())
        {
            for (const auto& completed : _blackboard.quest.completedQuests)
            {
                if (_blackboard.party.isGroupLeader &&
                    completed.questId == _blackboard.party.laggingQuestId)
                    continue;
                if (!_blackboard.party.isGroupLeader && _blackboard.party.isInGroup &&
                    std::find(_blackboard.party.leaderQuestIds.begin(),
                        _blackboard.party.leaderQuestIds.end(), completed.questId) !=
                        _blackboard.party.leaderQuestIds.end())
                    continue;
                if (_suppressions.IsQuestSuppressed(completed.questId))
                    continue;
                if (_questFailures.IsDeferred(completed.questId, bot->GetLevel()))
                    continue;
                if (inventoryCleanupDeferred || townServiceDeferred)
                {
                    Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
                    if (Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate))
                        continue;
                }

                if (completed.hasTurnInPosition)
                {
                    if (_suppressions.IsQuestDestinationSuppressed(
                        completed.turnInPosition.mapId,
                        completed.turnInPosition.x,
                        completed.turnInPosition.y, nowSec))
                    {
                        continue;
                    }
                    _activeQuestId = completed.questId;
                    if (TrySetTownRunGoal())
                        return;
                }
            }
        }

        // Priority 5: Progress active in-progress quests
        // Followers suspend independent travel when the leader gets beyond
        // the party leash. Once formation is restored they resume the shared
        // quest below, preserving normal loot/town/turn-in recovery above.
        if (_blackboard.party.isInGroup && !_blackboard.party.isGroupLeader &&
            _blackboard.party.groupLeaderGuid && _blackboard.party.leaderOnSameMap)
        {
            float leashThreshold = (_goal == BotGoal::FollowTarget) ? 12.0f : 30.0f;
            if (_blackboard.party.leaderDistance > leashThreshold)
            {
                // If the follower is standing close (<= 15y) to a questgiver offering an accepted leader quest,
                // allow the follower to accept the quest first before sprinting after the leader.
                bool hasNearbyLeaderQuestToAccept = false;
                for (uint32_t leaderQuestId : _blackboard.party.leaderQuestIds)
                {
                    auto available = std::find_if(_blackboard.quest.availableQuests.begin(),
                        _blackboard.quest.availableQuests.end(), [leaderQuestId, bot](const auto& quest) {
                            return quest.questId == leaderQuestId && quest.hasQuestGiverPosition &&
                                Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                                    quest.questGiverPosition.x, quest.questGiverPosition.y) <= 15.0f;
                        });
                    if (available != _blackboard.quest.availableQuests.end() &&
                        !_suppressions.IsQuestSuppressed(leaderQuestId, nowSec))
                    {
                        hasNearbyLeaderQuestToAccept = true;
                        break;
                    }
                }

                if (!hasNearbyLeaderQuestToAccept)
                {
                    SetGoal(BotGoal::FollowTarget);
                    return;
                }
            }
        }

        auto isSupportedDeathKnightIntroQuest = [bot](uint32_t questId) {
            return bot->GetClass() == CLASS_DEATH_KNIGHT &&
                bot->GetMapId() == 609 && questId == 12619;
        };
        auto isQuestSuitable = [this, bot, &isSupportedDeathKnightIntroQuest](uint32_t questId) {
            if (!isSupportedDeathKnightIntroQuest(questId) &&
                _questFailures.IsDeferred(questId, bot->GetLevel()))
                return false;
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
            if (questTemplate && Brain::IsExcludedQuest(questId,
                questTemplate->GetZoneOrSort()))
            {
                return false;
            }
            if (questTemplate && !Helper::IsQuestGroupStructureSuitable(
                bot->GetGroup() != nullptr, questTemplate->GetType(), questTemplate->GetSuggestedPlayers()))
            {
                return false;
            }
            return !questTemplate || Helper::IsQuestLevelSuitable(bot->GetLevel(),
                questTemplate->GetQuestLevel(), Config::BotConfig::GetQuestMaxLevelsAboveBot());
        };

        bool hasDeferredProgressionWork = (_activeQuestId != 0 &&
            _questFailures.IsDeferred(_activeQuestId, bot->GetLevel())) ||
            !_questFailures.GetDeferredQuestIds(bot->GetLevel()).empty() ||
            (_conservativeGrindUntilLevel != 0 && bot->GetLevel() < _conservativeGrindUntilLevel);
        std::vector<DangerArea> questTravelDangerAreas = GetTravelDangerAreas();
        auto canCommitQuestTravel = [&](uint32_t questId,
            const Common::PositionInfo& destination, const char* context) {
            if (_suppressions.IsQuestDestinationSuppressed(destination.mapId,
                destination.x, destination.y, nowSec))
            {
                hasDeferredProgressionWork = true;
                return false;
            }
            if (!Travel::WorldTravel::NeedsTravel(bot, destination, 20.0f))
                return true;

            Travel::TravelPreflightResult preflight =
                Travel::WorldTravel::Preflight(bot, destination,
                    questTravelDangerAreas,
                    _blackboard.spatial.hostileGuids);
            if (preflight.CanCommit())
                return true;

            QuestDestinationFailureDecision decision =
                _suppressions.RecordQuestDestinationFailure(
                    destination.mapId, destination.x, destination.y, nowSec);
            if (preflight.status == Travel::TravelPreflightStatus::Unreachable)
            {
                uint32_t suppressUntil = nowSec + SuppressionRegistry::QuestDestinationSuppressionSeconds;
                _suppressions.SuppressQuest(questId, suppressUntil);
                decision.escalated = true;
                decision.retryAfterSec = suppressUntil;
            }
            else if (decision.escalated)
                _suppressions.SuppressQuest(questId, decision.retryAfterSec);
            hasDeferredProgressionWork = true;

            Diagnostics::StructuredEvent event;
            event.event = "quest_travel_preflight_rejected";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction ? _activeAction->GetName() : "";
            event.actionInstance = _activeActionInstanceId;
            event.questId = questId;
            event.retryAfterSeconds = decision.retryAfterSec > nowSec
                ? decision.retryAfterSec - nowSec : 0;
            event.outcome = "Rejected";
            event.failureCategory = "Navigation";
            std::ostringstream details;
            details << "context=" << context
                << ";destination_map=" << destination.mapId
                << ";destination_x=" << destination.x
                << ";destination_y=" << destination.y
                << ";failure_count=" << decision.failureCount
                << ";hub_escalated=" << decision.escalated
                << ";reason=" << preflight.reason;
            event.details = details.str();
            AddMovementEvidence(event, _movement);
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));

            if (decision.escalated && Diagnostics::BotTrace::ShouldLog(
                bot, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' suppressed quest hub on map {} within {:.0f} yards for {} seconds after {} repeated first-leg failures",
                    bot->GetName(), destination.mapId,
                    SuppressionRegistry::QuestDestinationRadius,
                    SuppressionRegistry::QuestDestinationSuppressionSeconds,
                    decision.failureCount);
            }
            return false;
        };
        auto selectPreflightedQuest = [&](std::vector<QuestCandidate> candidates,
            const char* context) -> std::optional<uint32_t> {
            while (!candidates.empty())
            {
                auto selected = SelectBestQuest(candidates, bot->GetMapId(),
                    _blackboard.self.x, _blackboard.self.y,
                    _blackboard.self.z,
                    static_cast<uint64_t>(bot->GetGUID().GetCounter()));
                if (!selected)
                    return std::nullopt;
                auto candidate = std::find_if(candidates.begin(),
                    candidates.end(), [&](const QuestCandidate& value) {
                        return value.questId == *selected;
                    });
                if (candidate == candidates.end() ||
                    !candidate->hasTargetPosition)
                    return selected;

                Common::PositionInfo destination{ candidate->targetX,
                    candidate->targetY, candidate->targetZ,
                    candidate->targetMapId };
                if (canCommitQuestTravel(candidate->questId, destination,
                    context))
                {
                    return selected;
                }
                uint32_t rejectedQuestId = candidate->questId;
                candidates.erase(std::remove_if(candidates.begin(),
                    candidates.end(), [&](const QuestCandidate& value) {
                        return value.questId == rejectedQuestId;
                    }), candidates.end());
            }
            return std::nullopt;
        };
        std::vector<uint32_t> localQuestIds;
        localQuestIds.reserve(_blackboard.quest.activeQuests.size());
        for (const auto& quest : _blackboard.quest.activeQuests)
            localQuestIds.push_back(quest.questId);
        uint32_t coordinatedQuestId = (!_blackboard.party.isGroupLeader &&
            !_blackboard.party.leaderQuestIds.empty())
            ? Party::SelectSharedQuest(localQuestIds, _blackboard.party.leaderQuestIds, _activeQuestId)
            : 0;

        std::vector<QuestCandidate> candidates;
        for (const auto& q : _blackboard.quest.activeQuests)
        {
            if (coordinatedQuestId && q.questId != coordinatedQuestId)
                continue;
            bool forceDeathKnightIntro = isSupportedDeathKnightIntroQuest(q.questId);
            if ((!forceDeathKnightIntro && _suppressions.IsQuestSuppressed(q.questId)) ||
                !isQuestSuitable(q.questId) ||
                HasInventoryBlockedItemObjective(bot, q) ||
                (!forceDeathKnightIntro && q.hasTargetPosition &&
                 _suppressions.IsQuestDestinationSuppressed(
                    q.targetPosition.mapId, q.targetPosition.x,
                    q.targetPosition.y, nowSec)))
            {
                hasDeferredProgressionWork = true;
                continue;
            }

            QuestCandidate cand;
            cand.questId = q.questId;
            cand.hasTargetPosition = q.hasTargetPosition;
            cand.targetX = q.targetPosition.x;
            cand.targetY = q.targetPosition.y;
            cand.targetZ = q.targetPosition.z;
            cand.targetMapId = q.targetPosition.mapId;
            cand.isActiveQuest = (q.questId == _activeQuestId);
            cand.isCoordinatedQuest = (q.questId == coordinatedQuestId) ||
                forceDeathKnightIntro;
            cand.failureCount = _questFailures.GetFailureCount(
                q.questId, bot->GetLevel());
            candidates.push_back(cand);
        }

        // Active quests are already committed. Let ProgressQuestAction inspect
        // nearby live targets before WorldTravel owns the static objective
        // route; preflighting that authored position here could hide usable
        // live work. Persisted destination suppression still moves past a
        // route that has produced repeated terminal failures.
        auto bestQuest = SelectBestQuest(candidates, bot->GetMapId(),
            _blackboard.self.x, _blackboard.self.y, _blackboard.self.z,
            static_cast<uint64_t>(bot->GetGUID().GetCounter()));
        if (bestQuest)
        {
            _activeQuestId = *bestQuest;
            _activeTier = GoalTier::Progression;
            SetGoal(BotGoal::ProgressQuest);
            return;
        }
        _activeQuestId = 0;

        // Priority 6: Accept another quest when no active quest is currently
        // actionable. Nearby work wins in QuestSelector; a cached world starter
        // is the migration path after local questing is exhausted. Grind remains
        // below both rather than becoming the default response to one bad quest.
        if (!_blackboard.quest.availableQuests.empty())
        {
            if (!_blackboard.party.isGroupLeader)
            {
                for (uint32_t leaderQuestId : _blackboard.party.leaderQuestIds)
                {
                    auto available = std::find_if(_blackboard.quest.availableQuests.begin(),
                        _blackboard.quest.availableQuests.end(), [leaderQuestId](const auto& quest) {
                            return quest.questId == leaderQuestId && quest.hasQuestGiverPosition;
                        });
                    if (available != _blackboard.quest.availableQuests.end() &&
                        !_suppressions.IsQuestSuppressed(leaderQuestId) &&
                        isQuestSuitable(leaderQuestId) &&
                        canCommitQuestTravel(leaderQuestId,
                            available->questGiverPosition,
                            "party_quest_accept"))
                    {
                        _activeQuestId = leaderQuestId;
                        _activeTier = GoalTier::Progression;
                        SetGoal(BotGoal::AcceptQuest);
                        return;
                    }
                }
                if (!_blackboard.party.leaderQuestIds.empty())
                {
                    _activeTier = GoalTier::Coordination;
                    SetGoal(BotGoal::FollowTarget);
                    return;
                }
            }
            std::vector<QuestCandidate> availableCandidates;
            for (const auto& available : _blackboard.quest.availableQuests)
            {
                if (_suppressions.IsQuestSuppressed(available.questId) ||
                    !isQuestSuitable(available.questId))
                {
                    hasDeferredProgressionWork = true;
                    continue;
                }

                if (available.hasQuestGiverPosition)
                {
                    QuestCandidate candidate;
                    candidate.questId = available.questId;
                    candidate.hasTargetPosition = true;
                    candidate.targetX = available.questGiverPosition.x;
                    candidate.targetY = available.questGiverPosition.y;
                    candidate.targetZ = available.questGiverPosition.z;
                    candidate.targetMapId = available.questGiverPosition.mapId;
                    candidate.failureCount = _questFailures.GetFailureCount(
                        available.questId, bot->GetLevel());
                    availableCandidates.push_back(candidate);
                }
            }
            auto bestAvailableQuest = selectPreflightedQuest(
                std::move(availableCandidates), "quest_accept");
            if (bestAvailableQuest)
            {
                _activeQuestId = *bestAvailableQuest;
                _activeTier = GoalTier::Progression;
                SetGoal(BotGoal::AcceptQuest);
                return;
            }
        }

        if (_blackboard.party.isInGroup && !_blackboard.party.isGroupLeader &&
            _blackboard.party.groupLeaderGuid && !_blackboard.party.leaderQuestIds.empty())
        {
            _activeTier = GoalTier::Coordination;
            SetGoal(BotGoal::FollowTarget);
            return;
        }

        if (grindFallbackReady &&
            (hasDeferredProgressionWork || !_blackboard.quest.activeQuests.empty() ||
             !_blackboard.quest.availableQuests.empty()))
        {
            _activeTier = GoalTier::Fallback;
            SetGoal(BotGoal::Grind);
            return;
        }

        if (_blackboard.party.isInGroup && !_blackboard.party.isGroupLeader && _blackboard.party.groupLeaderGuid)
        {
            _activeTier = GoalTier::Coordination;
            SetGoal(BotGoal::FollowTarget);
            return;
        }

        _activeTier = GoalTier::Fallback;
        SetGoal(grindFallbackReady ? BotGoal::Grind :
            (wanderFallbackReady ? BotGoal::Wander : BotGoal::Idle));
    }

    bool BotBrain::TrySetTownRunGoal()
    {
        if (!Town::ShouldExecute(PreviewTownPlan()))
            return false;

        SetGoal(BotGoal::TownRun);
        return _goal == BotGoal::TownRun;
    }

    Town::Plan BotBrain::PreviewTownPlan() const
    {
        Player* bot = ResolveBot();
        if (!bot)
            return {};

        uint32_t nowSec = Helper::MonotonicSeconds();
        float routineVendorTravelDistance = Config::BotConfig::GetMaxRoutineVendorTravelDistance();
        float maxServiceTravelDistance = _deathRecovery.IsRecoveryActive(nowSec)
            ? std::min(120.0f, routineVendorTravelDistance) : routineVendorTravelDistance;
        bool inventoryCleanupDeferred = _inventoryCleanupRetryAfterSec != 0 &&
            nowSec < _inventoryCleanupRetryAfterSec &&
            _blackboard.inv.freeBagSlots <= _inventoryCleanupBlockedFreeSlots;
        bool townServiceDeferred = _townServiceRetryAfterSec != 0 &&
            nowSec < _townServiceRetryAfterSec;

        Town::PlanningInput input;
        input.freeBagSlots = _blackboard.inv.freeBagSlots;
        input.hasSellableItems = _blackboard.inv.hasItemsToSell;
        input.partyNeedsVendor = _blackboard.party.isGroupLeader &&
            _blackboard.party.memberNeedsTownRun &&
            _blackboard.party.memberNeedingTownRunGuid &&
            _blackboard.party.memberNeedingTownRunGuid != bot->GetGUID();
        input.partyMemberGuid = input.partyNeedsVendor
            ? _blackboard.party.memberNeedingTownRunGuid.GetRawValue() : 0;
        input.needsInventoryCleanup = !inventoryCleanupDeferred &&
            !townServiceDeferred &&
            (_blackboard.inv.lowBagSpace || _blackboard.inv.bagsFull ||
             Actions::LootAction::HasInventoryBlockedLoot(bot));
        if (!inventoryCleanupDeferred && !townServiceDeferred)
        {
            for (const auto& activeQuest : _blackboard.quest.activeQuests)
            {
                if (!_suppressions.IsQuestSuppressed(activeQuest.questId) &&
                    !_questFailures.IsDeferred(activeQuest.questId, bot->GetLevel()) &&
                    HasInventoryBlockedItemObjective(bot, activeQuest))
                {
                    // EvaluateGoals routes this condition to TownRun. Feed the
                    // same condition into the town planner so BuildActionRequest
                    // cannot turn that goal straight back into Idle.
                    input.hasQuestItemCapacityBlock = true;
                    break;
                }
            }
        }
        input.needsRepair = !townServiceDeferred &&
            _blackboard.inv.needsRepair;
        input.needsRestock = !townServiceDeferred &&
            _blackboard.inv.needsRestock &&
            (_restockRetryAfterSec == 0 || nowSec >= _restockRetryAfterSec);

        bool hasBlockedReward = false;
        for (const auto& completed : _blackboard.quest.completedQuests)
        {
            if (_blackboard.party.isGroupLeader &&
                completed.questId == _blackboard.party.laggingQuestId)
                continue;
            if (!_blackboard.party.isGroupLeader && _blackboard.party.isInGroup &&
                std::find(_blackboard.party.leaderQuestIds.begin(),
                    _blackboard.party.leaderQuestIds.end(), completed.questId) !=
                    _blackboard.party.leaderQuestIds.end())
                continue;
            if (_suppressions.IsQuestSuppressed(completed.questId))
                continue;
            if (_questFailures.IsDeferred(completed.questId, bot->GetLevel()))
                continue;

            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
            Town::QuestTurnInCandidate candidate;
            candidate.questId = completed.questId;
            candidate.liveRewardable =
                bot->GetQuestStatus(completed.questId) == QUEST_STATUS_COMPLETE;
            candidate.rewardBlocked = questTemplate &&
                Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate);
            if (inventoryCleanupDeferred && candidate.rewardBlocked)
                continue;
            candidate.hasKnownPosition = completed.hasTurnInPosition;
            hasBlockedReward = hasBlockedReward ||
                (candidate.rewardBlocked && candidate.hasKnownPosition);
            input.completedQuests.push_back(candidate);
        }

        bool needsRewardSpace = hasBlockedReward && input.freeBagSlots < input.rewardReserveSlots;
        Cache::PositionInfo servicePosition;
        uint32_t vendorEntry = 0;
        bool requireInventoryVendor = input.needsInventoryCleanup || needsRewardSpace || input.partyNeedsVendor;
        input.hasInventoryVendor = requireInventoryVendor &&
            FindVendorForInventory(bot, vendorEntry, true, false, &servicePosition,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);
        Cache::PositionInfo planningOrigin = input.hasInventoryVendor
            ? servicePosition : Cache::PositionInfo{ bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), bot->GetMapId() };

        vendorEntry = 0;
        input.hasRepairVendor = input.needsRepair &&
            FindVendorForInventory(bot, vendorEntry, false, true, nullptr,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);

        vendorEntry = 0;
        input.hasRestockVendor = input.needsRestock &&
            FindVendorForInventory(bot, vendorEntry, true, false, nullptr,
                &_suppressions.GetBlacklistedNpcs(), maxServiceTravelDistance);

        float originX = planningOrigin.x;
        float originY = planningOrigin.y;
        float originZ = planningOrigin.z;
        for (Town::QuestTurnInCandidate& candidate : input.completedQuests)
        {
            auto completed = std::find_if(_blackboard.quest.completedQuests.begin(),
                _blackboard.quest.completedQuests.end(), [&candidate](const auto& quest) {
                    return quest.questId == candidate.questId;
                });
            if (completed != _blackboard.quest.completedQuests.end() && completed->hasTurnInPosition)
            {
                candidate.distanceFromTownSq = completed->turnInPosition.mapId == bot->GetMapId()
                    ? Helper::DistanceSq(completed->turnInPosition.x, completed->turnInPosition.y,
                        completed->turnInPosition.z, originX, originY, originZ)
                    : std::numeric_limits<float>::max() / 2.0f;
            }
        }

        return Town::BuildPlan(input);
    }

    std::vector<DangerArea> BotBrain::GetTravelDangerAreas() const
    {
        std::vector<DangerArea> areas = _suppressions.GetDangerAreas();
        const auto& personalTravelAreas = _travelHazards.GetDangerAreas();
        areas.insert(areas.end(), personalTravelAreas.begin(),
            personalTravelAreas.end());
        return areas;
    }

    ActionRequest BotBrain::BuildActionRequest() const
    {
        auto wanderRequest = [&]() -> ActionRequest {
            WanderActionRequest wander;
            wander.origin = { _blackboard.self.x, _blackboard.self.y, _blackboard.self.z, _blackboard.self.mapId };
            wander.radius = 15.0f;
            wander.suppressedQuests = _suppressions.GetBlacklistedQuests();
            wander.suppressedDestinations =
                _suppressions.GetWanderDestinationSuppressions();
            return { BotGoal::Wander, std::move(wander) };
        };

        switch (_goal)
        {
            case BotGoal::Combat:
            {
                Player* bot = ResolveBot();
                ObjectGuid targetGuid = SelectCombatActionTarget(bot, _blackboard,
                    _combatInitiationMaxLevelOffset, _suppressions);
                return targetGuid ? ActionRequest{ _goal, TargetActionRequest{ targetGuid } }
                                  : ActionRequest{ BotGoal::Idle, std::monostate{} };
            }

            case BotGoal::FollowTarget:
            {
                ObjectGuid targetGuid = _blackboard.party.isGroupLeader
                    ? _blackboard.party.laggingQuestMemberGuid
                    : _blackboard.party.groupLeaderGuid;
                if (!targetGuid) targetGuid = _blackboard.spatial.nearestFriendlyGuid;
                return targetGuid ? ActionRequest{ _goal, FollowActionRequest{ targetGuid,
                                      _blackboard.party.formationDistance, _blackboard.party.formationAngle } }
                                  : ActionRequest{ BotGoal::Idle, std::monostate{} };
            }

            case BotGoal::RevivePartyMember:
                return _blackboard.party.deadGroupMemberGuid
                    ? ActionRequest{ _goal, TargetActionRequest{ _blackboard.party.deadGroupMemberGuid } }
                    : ActionRequest{ BotGoal::Idle, std::monostate{} };

            case BotGoal::Flee:
            {
                ObjectGuid targetGuid = _fleeThreatGuid;
                if (!targetGuid) targetGuid = _blackboard.combat.primaryAttackerGuid;
                if (!targetGuid) targetGuid = _blackboard.combat.currentTargetGuid;
                if (!targetGuid) targetGuid = _blackboard.spatial.nearestEnemyGuid;
                return targetGuid ? ActionRequest{ _goal, TargetActionRequest{ targetGuid } }
                                  : ActionRequest{ BotGoal::Idle, std::monostate{} };
            }

            case BotGoal::AcceptQuest:
            case BotGoal::TurnInQuest:
            case BotGoal::ProgressQuest:
                return { _goal, QuestActionRequest{
                    _activeQuestId, GetTravelDangerAreas() } };

            case BotGoal::MoveToNpc:
                if (!_blackboard.quest.availableQuests.empty())
                    return { _goal, MoveActionRequest{ _blackboard.quest.availableQuests.front().questGiverPosition } };
                if (!_blackboard.quest.completedQuests.empty())
                    return { _goal, MoveActionRequest{ _blackboard.quest.completedQuests.front().turnInPosition } };
                return { BotGoal::Idle, std::monostate{} };

            case BotGoal::Unstuck:
                return { _goal, UnstuckActionRequest{
                    _deathRecovery.GetDeadlyQuestId(),
                    _progressionRecoveryPending } };

            case BotGoal::TownRun:
            {
                Town::Plan plan = PreviewTownPlan();
                return !Town::ShouldExecute(plan)
                    ? ActionRequest{ BotGoal::Idle, std::monostate{} }
                    : ActionRequest{ _goal, TownRunActionRequest{ std::move(plan), _suppressions.GetBlacklistedNpcs(),
                        GetDeathRecoveryRemainingSeconds() > 0
                            ? std::min(120.0f, Config::BotConfig::GetMaxRoutineVendorTravelDistance())
                            : Config::BotConfig::GetMaxRoutineVendorTravelDistance(),
                        GetTravelDangerAreas() } };
            }

            case BotGoal::Wander:
                return wanderRequest();

            case BotGoal::Grind:
            {
                bool conservative = _conservativeGrindUntilLevel != 0 &&
                    _blackboard.self.level < _conservativeGrindUntilLevel;
                Helper::GrindingLevelBand band = Helper::SelectGrindingLevelBand(
                    Config::BotConfig::GetGrindMinLevelOffset(),
                    Config::BotConfig::GetGrindMaxLevelOffset(), conservative);
                return { _goal, GrindActionRequest{
                    band.minOffset,
                    band.maxOffset,
                    _suppressions.GetBlacklistedGrindSpawns(),
                    _suppressions.GetBlacklistedGrindEntries(),
                    _suppressions.GetGrindDestinationSuppressions(),
                    _suppressions.GetDangerAreas() } };
            }

            case BotGoal::Loot:
            case BotGoal::Vendor:
            case BotGoal::Rest:
            case BotGoal::Resurrect:
            case BotGoal::WaitForPartyResurrection:
            case BotGoal::Idle:
            default:
                return { _goal, std::monostate{} };
        }
    }

    void BotBrain::Think(uint32_t deltaMs)
    {
        Player* bot = ResolveBot();
        if (!bot) return;

        Diagnostics::SoakDigest::Touch(static_cast<uint32_t>(bot->GetGUID().GetCounter()), bot->GetName().c_str(), bot->GetLevel());
        if (!IsBlackboardDecisionReady()) return;

        if (!bot->IsAlive() || _blackboard.self.isDead)
            _partyDeathWaitMs += deltaMs;
        else
            _partyDeathWaitMs = 0;

        if (_blackboard.party.isGroupLeader && _blackboard.party.laggingQuestMemberGuid)
            _laggingMemberWaitMs += deltaMs;
        else
            _laggingMemberWaitMs = 0;

        if (_movement && _movement->IsExternallyControlled())
        {
            _externalControlLogTimerMs += deltaMs;
            if (_externalControlLogTimerMs >= 10000)
            {
                _externalControlLogTimerMs = 0;
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' brain paused by external movement control (Mode: {}, MovementState: {}, HasPath: {}).",
                        bot->GetName(), _movement->GetExternalControlModeName(),
                        _movement->GetStateName(), _movement->HasPath() ? "Yes" : "No");
                }
            }
            return;
        }
        _externalControlLogTimerMs = 0;

        if (_teleportTimerMs > deltaMs)
        {
            _teleportTimerMs -= deltaMs;
            return;
        }
        else
        {
            _teleportTimerMs = 0;
        }

        if (_activeAction && _activeAction->IsComplete())
        {
            Actions::ActionOutcome reportedOutcome = _activeAction->GetOutcome();
            Actions::ActionOutcome outcome = NormalizeTerminalOutcome(
                _activeAction->IsComplete(), reportedOutcome);
            _lastActionName = _activeAction->GetName();
            _lastActionOutcome = ActionOutcomeToString(outcome);
            _lastActionOutcomeReason = _activeAction->GetOutcomeReason();
            Actions::FailureCategory failureCategory = _activeAction->GetFailureCategory();
            Actions::RecoveryDirective recoveryDirective = _activeAction->GetRecoveryDirective();
            if (reportedOutcome == Actions::ActionOutcome::Running)
            {
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::ActionBugs);
                TC_LOG_ERROR("server", "[WorldBots] [Action] Bot '{}' action '{}' completed while still reporting Running; treating it as a retryable failure",
                    bot->GetName(), _activeAction->GetName());
            }
            uint32_t questId = _activeAction->GetRelatedQuestId();
            uint32_t npcEntry = _activeAction->GetRelatedNpcEntry();
            ObjectGuid relatedTargetGuid = _activeAction->GetRelatedTargetGuid();
            {
                Diagnostics::StructuredEvent event;
                event.event = "action_finished";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = questId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = RecoveryDirectiveToString(recoveryDirective);
                event.retryAfterSeconds = _activeAction->GetRetryDelaySeconds();
                event.details = _activeAction->GetOutcomeReason();
                AddMovementEvidence(event, _movement);
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            }
            if (_goal == BotGoal::Combat &&
                (failureCategory == Actions::FailureCategory::Navigation ||
                 failureCategory == Actions::FailureCategory::Transient) &&
                IsFailureOutcome(outcome) && relatedTargetGuid)
            {
                Creature* failedTarget = ObjectAccessor::GetCreature(
                    *bot, relatedTargetGuid);
                bool targetEngaged = failedTarget &&
                    (failedTarget->GetVictim() == bot ||
                     failedTarget->IsInCombatWith(bot));
                if (!targetEngaged)
                {
                    uint32_t untilSec = Helper::MonotonicSeconds() +
                        Combat::NonExecutableTargetSuppressionSeconds;
                    _suppressions.SuppressCombatTarget(
                        relatedTargetGuid.GetRawValue(), untilSec);
                    if (failedTarget && failedTarget->GetSpawnId() != 0)
                    {
                        _suppressions.SuppressGrindSpawn(
                            failedTarget->GetSpawnId(), untilSec);
                    }
                    if (Diagnostics::BotTrace::ShouldLog(
                        bot, Diagnostics::LogEvent::Normal))
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Combat] Bot '{}' suppressed non-executable target GUID {} for {} seconds after failing to engage it",
                            bot->GetName(), relatedTargetGuid.GetCounter(),
                            Combat::NonExecutableTargetSuppressionSeconds);
                    }
                }
            }
            bool personalTravelFailure =
                failureCategory == Actions::FailureCategory::Navigation &&
                IsFailureOutcome(outcome) &&
                _activeAction->IsWorldTravelInProgress();
            Common::PositionInfo questTravelDestination;
            bool hasQuestTravelDestination = questId != 0 &&
                _activeAction->GetTravelDestination(questTravelDestination);
            if (personalTravelFailure && hasQuestTravelDestination)
            {
                uint32_t destinationFailureNowSec = Helper::MonotonicSeconds();
                QuestDestinationFailureDecision decision =
                    _suppressions.RecordQuestDestinationFailure(
                        questTravelDestination.mapId,
                        questTravelDestination.x,
                        questTravelDestination.y,
                        destinationFailureNowSec);
                if (decision.escalated)
                    _suppressions.SuppressQuest(questId,
                        decision.retryAfterSec);

                Diagnostics::StructuredEvent event;
                event.event = decision.escalated
                    ? "quest_destination_suppressed"
                    : "quest_destination_retry_deferred";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = questId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(
                    failureCategory);
                event.recoveryDirective = RecoveryDirectiveToString(
                    recoveryDirective);
                event.retryAfterSeconds = decision.retryAfterSec >
                    destinationFailureNowSec
                    ? decision.retryAfterSec - destinationFailureNowSec : 0;
                std::ostringstream details;
                details << "destination_map=" << questTravelDestination.mapId
                    << ";destination_x=" << questTravelDestination.x
                    << ";destination_y=" << questTravelDestination.y
                    << ";failure_count=" << decision.failureCount
                    << ";hub_escalated=" << decision.escalated
                    << ";result=" << _activeAction->GetOutcomeReason();
                event.details = details.str();
                AddMovementEvidence(event, _movement);
                Diagnostics::StructuredEventLog::Write(bot,
                    std::move(event));

                if (decision.escalated &&
                    Diagnostics::BotTrace::ShouldLog(bot,
                        Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' suppressed the failed quest hub on map {} within {:.0f} yards for {} seconds; other quests at that hub will be skipped",
                        bot->GetName(), questTravelDestination.mapId,
                        SuppressionRegistry::QuestDestinationRadius,
                        SuppressionRegistry::QuestDestinationSuppressionSeconds);
                }
            }
            DangerArea travelFailureArea;
            bool hasTravelFailureArea =
                failureCategory == Actions::FailureCategory::Navigation &&
                IsFailureOutcome(outcome) &&
                _activeAction->GetTravelFailureArea(travelFailureArea);
            if (personalTravelFailure)
            {
                constexpr float FailedTravelAreaRadius = 45.0f;
                constexpr uint32_t FailedTravelAreaSuppressionSeconds = 900;
                uint32_t failureNowSec = Helper::MonotonicSeconds();
                if (!hasTravelFailureArea)
                {
                    travelFailureArea = { bot->GetMapId(), bot->GetPositionX(),
                        bot->GetPositionY(), FailedTravelAreaRadius, 0 };
                }
                _travelHazards.SuppressDangerArea(travelFailureArea.mapId,
                    travelFailureArea.x, travelFailureArea.y,
                    travelFailureArea.radius > 0.0f
                        ? travelFailureArea.radius : FailedTravelAreaRadius,
                    failureNowSec + FailedTravelAreaSuppressionSeconds);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Travel] Bot '{}' personally blacklisted the area around its failed travel leg within {:.0f} yards for {} seconds; combat and grinding remain unaffected",
                        bot->GetName(), FailedTravelAreaRadius,
                        FailedTravelAreaSuppressionSeconds);
                }
            }
            if (ShouldApplyRestockBackoff(_goal, outcome, _blackboard.inv.needsRestock))
            {
                constexpr uint32_t RestockBackoffSeconds = 300;
                _restockRetryAfterSec = Helper::MonotonicSeconds() + RestockBackoffSeconds;
            }
            if (_goal == BotGoal::Rest &&
                failureCategory == Actions::FailureCategory::ServiceCapability &&
                IsFailureOutcome(outcome))
            {
                uint32_t restBackoffNowSec = Helper::MonotonicSeconds();
                _restRecoveryBackoff.Begin(restBackoffNowSec);
                Diagnostics::StructuredEvent event;
                event.event = "rest_backoff_started";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = RecoveryDirectiveToString(recoveryDirective);
                event.retryAfterSeconds =
                    Helper::RestRecoveryBackoffPolicy::BackoffSeconds;
                event.details = _activeAction->GetOutcomeReason();
                AddMovementEvidence(event, _movement);
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            }
            else if (_goal == BotGoal::Rest &&
                outcome == Actions::ActionOutcome::Succeeded)
            {
                _restRecoveryBackoff.RecordRecovery();
            }
            uint32_t townServiceBackoffSeconds = GetTownServiceBackoffSeconds(
                _goal, outcome, failureCategory);
            if (townServiceBackoffSeconds != 0)
            {
                _townServiceRetryAfterSec = Helper::MonotonicSeconds() +
                    townServiceBackoffSeconds;
                if (Diagnostics::BotTrace::ShouldLog(
                    bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Town] Bot '{}' could not complete required town service; deferring vendor-dependent work for {} seconds and replanning into other progression",
                        bot->GetName(), townServiceBackoffSeconds);
                }
            }
            if (_activeAction->IsInventoryCapacityFailure() ||
                ShouldApplyInventoryCleanupBackoff(_goal, outcome,
                    failureCategory))
            {
                constexpr uint32_t InventoryCleanupBackoffSeconds = 900;
                _inventoryCleanupRetryAfterSec = Helper::MonotonicSeconds() + InventoryCleanupBackoffSeconds;
                _inventoryCleanupBlockedFreeSlots = _blackboard.inv.freeBagSlots;
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::InventoryDeadlocks);
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Inventory] Bot '{}' deferred inventory cleanup for {} seconds because no additional safe space could be created; protected items will not be destroyed",
                        bot->GetName(), InventoryCleanupBackoffSeconds);
                }
            }
            if (questId != 0 &&
                ShouldSuppressQuest(outcome, failureCategory, recoveryDirective))
            {
                uint32_t suppressSeconds = std::max<uint32_t>(
                    GetSuppressionSeconds(outcome),
                    _activeAction->GetRetryDelaySeconds());
                _suppressions.SuppressQuest(questId, Helper::MonotonicSeconds() + suppressSeconds);
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::QuestsSuppressed);
                RememberQuestFailure(bot, questId,
                    ClassifyQuestFailure(outcome, failureCategory),
                    _activeAction->GetOutcomeReason().empty()
                        ? "an unspecified action failure"
                        : _activeAction->GetOutcomeReason().c_str());
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' suspended quest {} for {} seconds after action outcome {}: {}",
                        bot->GetName(), questId, suppressSeconds, static_cast<uint32_t>(outcome),
                        _activeAction->GetOutcomeReason().empty() ? "unspecified failure" : _activeAction->GetOutcomeReason());
                }
                if (_activeQuestId == questId)
                    _activeQuestId = 0;
            }
            else if (questId != 0 && outcome == Actions::ActionOutcome::Succeeded)
            {
                _questFailures.RecordSuccess(questId);
                if (hasQuestTravelDestination)
                {
                    _suppressions.RecordQuestDestinationSuccess(
                        questTravelDestination.mapId,
                        questTravelDestination.x,
                        questTravelDestination.y);
                }
            }
            if (npcEntry != 0 && IsFailureOutcome(outcome))
            {
                uint32_t suppressSeconds = GetSuppressionSeconds(outcome);
                _suppressions.SuppressNpc(npcEntry, Helper::MonotonicSeconds() + suppressSeconds);
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::NpcsSuppressed);
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] Bot '{}' suspended NPC Entry {} for {} seconds after action outcome {}: {}",
                        bot->GetName(), npcEntry, suppressSeconds, static_cast<uint32_t>(outcome),
                        _activeAction->GetOutcomeReason().empty() ? "unspecified failure" : _activeAction->GetOutcomeReason());
                }
            }

            if (_goal == BotGoal::Unstuck)
            {
                if (auto* unstuck = dynamic_cast<Actions::UnstuckAction*>(
                    _activeAction.get()); unstuck &&
                    unstuck->IsProgressionRecovery())
                {
                    bool materiallyChangedEcology =
                        outcome == Actions::ActionOutcome::Succeeded &&
                        unstuck->DidMateriallyChangeEcology();
                    _progressionRecovery.RecordRelocationResult(
                        Helper::MonotonicSeconds(), materiallyChangedEcology);

                    Diagnostics::StructuredEvent event;
                    event.event = "progression_relocation_result";
                    event.goal = BotGoalToString(_goal);
                    event.action = _activeAction->GetName();
                    event.actionInstance = _activeActionInstanceId;
                    event.outcome = ActionOutcomeToString(outcome);
                    event.failureCategory = FailureCategoryToString(
                        failureCategory);
                    event.recoveryDirective = RecoveryDirectiveToString(
                        recoveryDirective);
                    std::ostringstream details;
                    details << "material_ecology_change="
                        << materiallyChangedEcology
                        << ";remaining_recovery_budget="
                        << (ProgressionRecoveryPolicy::MaxStallRecoveriesWithoutProgress -
                            _progressionRecovery.GetStallRecoveryCount())
                        << ";result=" << _activeAction->GetOutcomeReason();
                    event.details = details.str();
                    AddMovementEvidence(event, _movement);
                    Diagnostics::StructuredEventLog::Write(bot,
                        std::move(event));
                }
                _navigationRecoveryPending = false;
                _progressionRecoveryPending = false;
                _townServiceRecoveryPending = false;
                _combatStallRecoveryPending = false;
                _fleeRecoveryPending = false;
                if (outcome == Actions::ActionOutcome::Succeeded)
                {
                    if (_movement)
                        _movement->ResetOriginPathRecovery();
                    // Clear the failure window without clearing the short
                    // post-relocation cooldown established by the trigger.
                    _navigationRecovery.RecordSuccess();
                    _fleeRecovery.RecordSuccess();
                    _townServiceRecovery.RecordSuccess();
                }
            }
            else if (_goal == BotGoal::Combat &&
                outcome == Actions::ActionOutcome::RetryableFailure &&
                failureCategory == Actions::FailureCategory::Stalled)
            {
                ObjectGuid stalledTargetGuid = _activeAction->GetRelatedTargetGuid();
                if (stalledTargetGuid)
                {
                    constexpr uint32_t CombatStallSuppressionSeconds = 600; // 10 minutes
                    _suppressions.SuppressCombatTarget(stalledTargetGuid.GetRawValue(),
                        Helper::MonotonicSeconds() + CombatStallSuppressionSeconds);
                }

                Diagnostics::StructuredEvent event;
                event.event = "combat_stall_relocation_triggered";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = "Relocate";
                event.details = _activeAction->GetOutcomeReason();
                AddMovementEvidence(event, _movement);
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' exhausted bounded exact-target combat recovery; relocating to a level-safe friendly travel hub",
                    bot->GetName());
                _combatStallRecoveryPending = true;
                SetGoal(BotGoal::Unstuck);
                return;
            }
            else if (_goal == BotGoal::Flee &&
                outcome == Actions::ActionOutcome::Blocked &&
                failureCategory == Actions::FailureCategory::Navigation)
            {
                uint32_t failureNowSec = Helper::MonotonicSeconds();
                bool recoveryTriggered = _fleeRecovery.RecordTimeout(
                    failureNowSec, bot->GetMapId(), bot->GetPositionX(),
                    bot->GetPositionY(), bot->GetPositionZ());

                Diagnostics::StructuredEvent event;
                event.event = recoveryTriggered
                    ? "flee_relocation_triggered" : "flee_timeout_recorded";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = recoveryTriggered
                    ? "Relocate" : RecoveryDirectiveToString(recoveryDirective);
                AddMovementEvidence(event, _movement);
                std::ostringstream details;
                details << "same_origin_timeouts="
                    << _fleeRecovery.GetFailureCount()
                    << ";relocation_threshold="
                    << FleeRecoveryPolicy::FailureThreshold
                    << ";window_s="
                    << FleeRecoveryPolicy::FailureWindowSeconds
                    << ";recovery_cooldown_s="
                    << _fleeRecovery.GetRecoveryCooldownRemaining(failureNowSec)
                    << ";last_failure=" << _activeAction->GetOutcomeReason();
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));

                if (recoveryTriggered)
                {
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' timed out {} flee attempts within {} seconds without leaving a {:.0f}-yard origin; relocating to a level-safe friendly travel hub",
                        bot->GetName(), FleeRecoveryPolicy::FailureThreshold,
                        FleeRecoveryPolicy::FailureWindowSeconds,
                        FleeRecoveryPolicy::FailureOriginRadius);
                    // Flee remains the highest tactical priority while the
                    // stale attacker/combat state is still present. Latch the
                    // relocation so the next goal evaluation cannot preempt
                    // Unstuck before its action starts and clears combat.
                    _fleeRecoveryPending = true;
                    SetGoal(BotGoal::Unstuck);
                    return;
                }
            }
            else if (_goal == BotGoal::Grind &&
                failureCategory == Actions::FailureCategory::ProgressionDifficulty &&
                IsFailureOutcome(outcome))
            {
                // GrindAction learns stale or unreachable authored anchors while
                // expanding its search. Carry that evidence across action
                // instances; otherwise the retry backoff recreates GrindAction
                // with an empty local blacklist and immediately selects the
                // same three rejected destinations again.
                uint32_t grindFailureNowSec = Helper::MonotonicSeconds();
                std::size_t persistedAnchorSuppressions = 0;
                if (auto* grind = dynamic_cast<Actions::GrindAction*>(
                    _activeAction.get()))
                {
                    persistedAnchorSuppressions =
                        _suppressions.PersistGrindAnchorSuppressions(
                            grind->GetSuppressedSpawnIds(),
                            grind->GetSuppressedCreatureEntries(),
                            grindFailureNowSec);
                    persistedAnchorSuppressions +=
                        _suppressions.PersistGrindDestinationSuppressions(
                            grind->GetSuppressedDestinations(),
                            grindFailureNowSec);
                }
                if (persistedAnchorSuppressions != 0)
                {
                    Diagnostics::StructuredEvent event;
                    event.event = "grind_anchor_suppressions_persisted";
                    event.goal = BotGoalToString(_goal);
                    event.action = _activeAction->GetName();
                    event.actionInstance = _activeActionInstanceId;
                    event.outcome = ActionOutcomeToString(outcome);
                    event.failureCategory = FailureCategoryToString(
                        failureCategory);
                    std::ostringstream details;
                    details << "active_anchor_suppressions="
                        << persistedAnchorSuppressions;
                    event.details = details.str();
                    AddMovementEvidence(event, _movement);
                    Diagnostics::StructuredEventLog::Write(bot,
                        std::move(event));
                }
                bool recoveryTriggered = _progressionRecovery.RecordGrindFailure(
                    grindFailureNowSec);
                if (recoveryTriggered)
                {
                    _grindRetryAfterSec = 0;
                    _progressionRecoveryPending = true;
                    Diagnostics::StructuredEvent event;
                    event.event = "progression_ecology_relocation_triggered";
                    event.goal = BotGoalToString(_goal);
                    event.action = _activeAction->GetName();
                    event.actionInstance = _activeActionInstanceId;
                    event.questId = questId;
                    event.outcome = ActionOutcomeToString(outcome);
                    event.failureCategory = FailureCategoryToString(failureCategory);
                    event.recoveryDirective = "Relocate";
                    AddMovementEvidence(event, _movement);
                    std::ostringstream details;
                    details << "complete_grind_failures="
                        << ProgressionRecoveryPolicy::FailureThreshold
                        << ";window_s="
                        << ProgressionRecoveryPolicy::FailureWindowSeconds
                        << ";recovery_attempt="
                        << _progressionRecovery.GetStallRecoveryCount()
                        << ";recovery_limit="
                        << ProgressionRecoveryPolicy::MaxStallRecoveriesWithoutProgress
                        << ";recovery_cooldown_s="
                        << _progressionRecovery.GetRecoveryCooldownRemaining(
                            Helper::MonotonicSeconds())
                        << ";last_failure="
                        << _activeAction->GetOutcomeReason();
                    event.details = details.str();
                    Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' exhausted its complete grinding search {} times without gaining XP; relocating to a procedurally selected level-safe ecology",
                        bot->GetName(), ProgressionRecoveryPolicy::FailureThreshold);
                    SetGoal(BotGoal::Unstuck);
                    return;
                }

                // Once all bounded ecology relocations have failed, repeating
                // the same complete three-anchor search every minute only
                // recreates proven procedural work. Give Wander and other
                // progression goals a longer window to change the ecology;
                // any XP gain or material relocation restores the ordinary
                // responsive backoff through ProgressionRecoveryPolicy.
                uint32_t grindRetryBackoffSeconds =
                    _progressionRecovery.GetGrindRetryBackoffSeconds();
                _grindRetryAfterSec = Helper::MonotonicSeconds() +
                    grindRetryBackoffSeconds;
                Diagnostics::StructuredEvent event;
                event.event = "grind_backoff_started";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = questId;
                event.retryAfterSeconds = grindRetryBackoffSeconds;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = RecoveryDirectiveToString(recoveryDirective);
                std::ostringstream details;
                details << "complete_grind_failures="
                    << _progressionRecovery.GetFailureCount()
                    << ";relocation_threshold="
                    << ProgressionRecoveryPolicy::FailureThreshold
                    << ";recovery_budget_exhausted="
                    << _progressionRecovery.IsRecoveryBudgetExhausted()
                    << ";last_failure=" << _activeAction->GetOutcomeReason();
                event.details = details.str();
                AddMovementEvidence(event, _movement);
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Grind] Bot '{}' found no safe level-appropriate hunting destination; pausing grind fallback for {} seconds",
                        bot->GetName(), grindRetryBackoffSeconds);
                }
            }
            else if (_goal == BotGoal::TownRun &&
                failureCategory == Actions::FailureCategory::Navigation &&
                IsFailureOutcome(outcome))
            {
                uint32_t failureNowSec = Helper::MonotonicSeconds();
                bool recoveryTriggered =
                    _townServiceRecovery.RecordNavigationFailure(failureNowSec);
                Diagnostics::StructuredEvent event;
                event.event = recoveryTriggered
                    ? "town_service_relocation_triggered"
                    : "town_service_navigation_failure_recorded";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = questId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = recoveryTriggered
                    ? "Relocate" : RecoveryDirectiveToString(recoveryDirective);
                AddMovementEvidence(event, _movement);
                std::ostringstream details;
                details << "town_navigation_failures="
                    << _townServiceRecovery.GetFailureCount()
                    << ";relocation_threshold="
                    << TownServiceRecoveryPolicy::FailureThreshold
                    << ";window_s="
                    << TownServiceRecoveryPolicy::FailureWindowSeconds
                    << ";recovery_cooldown_s="
                    << _townServiceRecovery.GetRecoveryCooldownRemaining(
                        failureNowSec)
                    << ";last_failure=" << _activeAction->GetOutcomeReason();
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));

                // The destination-level evidence is owned by the town policy;
                // it must not contaminate the stable-origin navmesh detector.
                _navigationRecovery.RecordSuccess();
                if (recoveryTriggered)
                {
                    _townServiceRecoveryPending = true;
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' failed {} complete town-service navigation attempts within {} seconds; relocating to a procedurally selected level-safe friendly hub",
                        bot->GetName(), TownServiceRecoveryPolicy::FailureThreshold,
                        TownServiceRecoveryPolicy::FailureWindowSeconds);
                    SetGoal(BotGoal::Unstuck);
                    return;
                }
            }
            else if (_goal == BotGoal::Wander)
            {
                auto* wander = dynamic_cast<Actions::WanderAction*>(
                    _activeAction.get());
                if (wander && wander->RequiresOriginRecovery() &&
                    failureCategory == Actions::FailureCategory::Navigation &&
                    IsFailureOutcome(outcome))
                {
                    uint32_t recoveryNowSec = Helper::MonotonicSeconds();
                    uint32_t wanderBackoffSeconds =
                        _wanderRecoveryBackoff.Begin(recoveryNowSec);
                    bool promoteToProgressionRecovery =
                        _progressionRecovery.ShouldPromoteWanderRecoveryToProgression();
                    if (promoteToProgressionRecovery)
                    {
                        // The Wander backoff already bounds this new recovery
                        // epoch to at most once per thirty minutes. Reconsider
                        // hubs rejected by the exhausted epoch and require the
                        // replacement to prove a reachable grinding ecology.
                        Actions::UnstuckAction::ResetRecoveryCandidates(
                            bot->GetGUID());
                        _progressionRecoveryPending = true;
                    }
                    else
                    {
                        _navigationRecoveryPending = true;
                    }
                    Diagnostics::StructuredEvent event;
                    event.event = "wander_origin_relocation_triggered";
                    event.goal = BotGoalToString(_goal);
                    event.action = _activeAction->GetName();
                    event.actionInstance = _activeActionInstanceId;
                    event.outcome = ActionOutcomeToString(outcome);
                    event.failureCategory = FailureCategoryToString(
                        failureCategory);
                    event.recoveryDirective = "Relocate";
                    event.retryAfterSeconds = wanderBackoffSeconds;
                    std::ostringstream wanderDetails;
                    wanderDetails << _activeAction->GetOutcomeReason()
                        << ";consecutive_recoveries="
                        << _wanderRecoveryBackoff.GetConsecutiveRecoveryCount()
                        << ";retry_after_s=" << wanderBackoffSeconds
                        << ";promoted_to_progression_recovery="
                        << promoteToProgressionRecovery;
                    event.details = wanderDetails.str();
                    AddMovementEvidence(event, _movement);
                    Diagnostics::StructuredEventLog::Write(bot,
                        std::move(event));
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' exhausted productive Wander destinations and {} exploration cycles without starting movement; relocating to a level-safe friendly travel hub",
                        bot->GetName(),
                        Helper::WanderRecoveryPolicy::FailureThreshold);
                    SetGoal(BotGoal::Unstuck);
                    return;
                }
            }
            else if (ShouldRelocateForNavigationFailure(_goal, outcome,
                failureCategory, _movement->GetLastPathFlags(),
                _blackboard.nav.isStuck ||
                    _movement->GetState() == BotMovementState::Stuck,
                _movement->GetPathAttemptGeneration() !=
                    _activeActionPathAttemptGeneration))
            {
                bool recoveryTriggered = _navigationRecovery.RecordFailure(
                    Helper::MonotonicSeconds(),
                    bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(),
                    bot->GetPositionZ(),
                    hasTravelFailureArea ? travelFailureArea.x : _movement->GetDestinationX(),
                    hasTravelFailureArea ? travelFailureArea.y : _movement->GetDestinationY(),
                    hasTravelFailureArea ? bot->GetPositionZ() : _movement->GetDestinationZ());
                Diagnostics::StructuredEvent event;
                event.event = recoveryTriggered
                    ? "navigation_relocation_triggered"
                    : "navigation_failure_recorded";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = questId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = RecoveryDirectiveToString(recoveryDirective);
                AddMovementEvidence(event, _movement);
                std::ostringstream details;
                details << "recovery_failure_count="
                    << _navigationRecovery.GetFailureCount()
                    << ";recovery_cooldown_s="
                    << _navigationRecovery.GetRecoveryCooldownRemaining(
                        Helper::MonotonicSeconds())
                    << ";triggered=" << recoveryTriggered;
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                if (recoveryTriggered)
                {
                    _navigationRecoveryPending = true;
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' encountered {} distinct navigation leg failures within {} seconds while remaining near the same position; relocating to a level-safe friendly travel hub",
                        bot->GetName(), NavigationRecoveryPolicy::FailureThreshold,
                        NavigationRecoveryPolicy::FailureWindowSeconds);
                    SetGoal(BotGoal::Unstuck);
                    return;
                }
            }
            else if (ShouldRecordNavigationFailure(_goal, outcome,
                failureCategory) &&
                !ShouldRelocateForNavigationFailure(_goal, outcome,
                    failureCategory, _movement->GetLastPathFlags(),
                    _blackboard.nav.isStuck ||
                        _movement->GetState() == BotMovementState::Stuck,
                    _movement->GetPathAttemptGeneration() !=
                        _activeActionPathAttemptGeneration))
            {
                Diagnostics::StructuredEvent event;
                event.event = "navigation_failure_not_relocatable";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = questId;
                event.outcome = ActionOutcomeToString(outcome);
                event.failureCategory = FailureCategoryToString(failureCategory);
                event.recoveryDirective = RecoveryDirectiveToString(recoveryDirective);
                AddMovementEvidence(event, _movement);
                std::ostringstream details;
                details << "fresh_path_evidence="
                    << (_movement->GetPathAttemptGeneration() !=
                        _activeActionPathAttemptGeneration)
                    << ";nav_stuck=" << (_blackboard.nav.isStuck ||
                        _movement->GetState() == BotMovementState::Stuck)
                    << ";decision=destination_or_route_failure";
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                // This was a bad destination or route. Quest/NPC suppression
                // above owns the recovery; do not let it contribute toward a
                // later relocation of an otherwise healthy bot.
                _navigationRecovery.RecordSuccess();
            }
            else if (outcome == Actions::ActionOutcome::Succeeded)
            {
                _navigationRecovery.RecordSuccess();
                if (_goal == BotGoal::Flee)
                    _fleeRecovery.RecordSuccess();
                if (_goal == BotGoal::TownRun)
                    _townServiceRecovery.RecordSuccess();
                if (_goal == BotGoal::TurnInQuest)
                {
                    uint32_t turnedInQ = questId != 0 ? questId : _activeQuestId;
                    if (turnedInQ != 0)
                    {
                        _questStruggles.erase(turnedInQ);
                        Party::PartyRecruitmentPolicy::CheckAndDisbandIfCompleted(bot, turnedInQ);
                    }
                }

                for (auto it = _questStruggles.begin(); it != _questStruggles.end(); )
                {
                    if (bot->GetQuestStatus(it->first) != QUEST_STATUS_INCOMPLETE)
                        it = _questStruggles.erase(it);
                    else
                        ++it;
                }
            }
        }

        const Blackboard::ActiveQuest* watchedQuest = nullptr;
        for (const auto& quest : _blackboard.quest.activeQuests)
        {
            if (quest.questId == _activeQuestId)
            {
                watchedQuest = &quest;
                break;
            }
        }
        if (watchedQuest)
        {
            bool activelyExecutingQuest = _goal == BotGoal::ProgressQuest &&
                _activeAction && !_activeAction->IsComplete() &&
                _activeAction->GetRelatedQuestId() == watchedQuest->questId;
            uint64_t signature = CalculateQuestProgressSignature(*watchedQuest);
            if (activelyExecutingQuest)
            {
                uint64_t activitySignature = _activeAction->GetProgressActivitySignature();
                if (activitySignature != 0)
                {
                    signature ^= activitySignature + 0x9e3779b97f4a7c15ULL +
                        (signature << 6) + (signature >> 2);
                }
            }

            bool travellingToObjective = false;
            if (activelyExecutingQuest && _activeAction->IsWorldTravelInProgress())
            {
                travellingToObjective = true;
            }
            else if (activelyExecutingQuest && watchedQuest->hasTargetPosition && _movement &&
                     _movement->GetState() != BotMovementState::Idle)
            {
                travellingToObjective = Helper::DistanceSq(
                    _blackboard.self.x, _blackboard.self.y, _blackboard.self.z,
                    watchedQuest->targetPosition.x, watchedQuest->targetPosition.y,
                    watchedQuest->targetPosition.z) > 900.0f;
            }

            _progressWatchdog.Update(watchedQuest->questId, signature, activelyExecutingQuest, travellingToObjective, deltaMs);

            if (_progressWatchdog.IsStalled())
            {
                Diagnostics::StructuredEvent event;
                event.event = "quest_progress_stalled";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction ? _activeAction->GetName() : "";
                event.actionInstance = _activeActionInstanceId;
                event.questId = watchedQuest->questId;
                AddMovementEvidence(event, _movement);
                std::ostringstream details;
                details << "watchdog_ms=" << _progressWatchdog.GetNoChangeMs()
                    << ";progress_signature=" << signature
                    << ";actively_executing=" << activelyExecutingQuest
                    << ";travelling=" << travellingToObjective;
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                _suppressions.SuppressQuest(watchedQuest->questId, Helper::MonotonicSeconds() + 900);
                RememberQuestFailure(bot, watchedQuest->questId,
                    QuestFailureKind::Stalled,
                    "the objective progress watchdog stalled");
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' suspended quest {} after 180 seconds without objective counter, inventory, or target-health progress",
                        bot->GetName(), watchedQuest->questId);
                }
                _activeQuestId = 0;
                _progressWatchdog.Reset();
            }
        }
        else
        {
            _progressWatchdog.Reset();
        }

        EvaluateGoals();

        if (_activeAction && !_activeAction->IsComplete())
        {
            if (_activeAction->TryUpdateContext(bot, _blackboard))
            {
                _transitionMetrics.contextRefreshes++;
            }
        }

        // WorldTravel owns per-edge timeouts and alternate-route recovery.
        // The generic detector's graveyard teleport would destroy transport,
        // portal, and flight state while those transitions are legitimately
        // waiting with an idle MovementManager.
        bool worldTravelActive = _activeAction && _activeAction->IsWorldTravelInProgress();
        if (worldTravelActive)
            _stuckDetector.Reset();
        else if (_stuckDetector.Update(bot, _movement, _goal, _blackboard, _activeQuestId,
                                      _suppressions, deltaMs))
        {
            _recoveryPauseMs = 1000;
            if (_stuckDetector.IsSevereStuck())
            {
                _stuckDetector.Reset();
                _navigationRecoveryPending = true;
                TC_LOG_WARN("server", "[WorldBots] [Brain] Bot '{}' severe 12s deadlock detected by StuckDetector; triggering Unstuck goal",
                    bot->GetName());
                SetGoal(BotGoal::Unstuck);
                return;
            }
        }

        // Manage Action transitions via ActionFactory
        if (!_activeAction || _activeAction->IsComplete())
        {
            ActionRequest request = BuildActionRequest();
            if (request.goal != _goal)
            {
                SetGoal(request.goal);
                if (_goal != request.goal)
                    return;
            }
            auto newAction = Actions::ActionFactory::CreateAction(request);
            if (newAction)
            {
                SetAction(std::move(newAction));
            }
        }
    }

    void BotBrain::UpdateAction(uint32_t deltaMs)
    {
        Player* bot = ResolveBot();
        if (!bot) return;
        if (!IsBlackboardDecisionReady()) return;
        if (_movement && _movement->IsExternallyControlled()) return;
        if (_recoveryPauseMs > 0)
        {
            _recoveryPauseMs = _recoveryPauseMs > deltaMs ? _recoveryPauseMs - deltaMs : 0;
            return;
        }
        if (_activeAction && _movement)
        {
            _movement->SetDiagnosticContext(BotGoalToString(_goal),
                _activeAction->GetName(), _activeActionInstanceId,
                _activeAction->GetRelatedQuestId() != 0
                    ? _activeAction->GetRelatedQuestId() : _activeQuestId);
            _activeActionElapsedMs = deltaMs >
                    std::numeric_limits<uint32_t>::max() - _activeActionElapsedMs
                ? std::numeric_limits<uint32_t>::max()
                : _activeActionElapsedMs + deltaMs;
            bool ordinaryIdle = _movement->GetState() == BotMovementState::Idle &&
                !_activeAction->IsWorldTravelInProgress() && !bot->IsInCombat() &&
                _goal != BotGoal::Idle &&
                _goal != BotGoal::WaitForPartyResurrection;
            _activeActionIdleMs = ordinaryIdle
                ? (deltaMs > std::numeric_limits<uint32_t>::max() - _activeActionIdleMs
                    ? std::numeric_limits<uint32_t>::max()
                    : _activeActionIdleMs + deltaMs)
                : 0;

            constexpr uint32_t ActionHardTimeoutMs = 15 * 60 * 1000;
            constexpr uint32_t OrdinaryIdleTimeoutMs = 2 * 60 * 1000;
            if (!_activeAction->IsComplete() &&
                (_activeActionElapsedMs >= ActionHardTimeoutMs ||
                 _activeActionIdleMs >= OrdinaryIdleTimeoutMs))
            {
                std::string reason = _activeActionElapsedMs >= ActionHardTimeoutMs
                    ? "action exceeded its 15-minute hard timeout"
                    : "action remained idle without world travel for 2 minutes";
                bool freshRejectedPath =
                    _movement->GetPathAttemptGeneration() !=
                        _activeActionPathAttemptGeneration &&
                    _movement->GetLastPathFailure() != BotPathFailure::None;
                Actions::FailureCategory timeoutCategory = freshRejectedPath
                    ? Actions::FailureCategory::Navigation
                    : (_goal == BotGoal::ProgressQuest
                        ? Actions::FailureCategory::Stalled
                        : Actions::FailureCategory::Transient);
                Actions::RecoveryDirective timeoutDirective = Actions::RecoveryDirective::RetryLater;
                if (freshRejectedPath)
                {
                    reason += std::string("; latest path request failed: ") +
                        _movement->GetLastPathFailureName();
                }
                Diagnostics::StructuredEvent event;
                event.event = "action_timeout";
                event.goal = BotGoalToString(_goal);
                event.action = _activeAction->GetName();
                event.actionInstance = _activeActionInstanceId;
                event.questId = _activeAction->GetRelatedQuestId();
                event.outcome = "RetryableFailure";
                event.failureCategory = FailureCategoryToString(timeoutCategory);
                event.recoveryDirective = RecoveryDirectiveToString(timeoutDirective);
                event.details = reason;
                AddMovementEvidence(event, _movement);
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                TC_LOG_WARN("server", "[WorldBots] [Action] Bot '{}' aborting '{}' because {}",
                    bot->GetName(), _activeAction->GetName(), reason);
                Diagnostics::SoakDigest::Record(
                    static_cast<uint32_t>(bot->GetGUID().GetCounter()),
                    Diagnostics::SoakEvent::ActionBugs);
                _activeAction->Abort(std::move(reason),
                    timeoutCategory, timeoutDirective);
                return;
            }
            _activeAction->Update(bot, _movement, _blackboard, deltaMs);
        }
    }

    void BotBrain::SetGoal(BotGoal newGoal)
    {
        Player* bot = ResolveBot();

        // Goal evaluation and action creation can straddle a refreshed
        // blackboard snapshot. If the town work disappeared in between, do
        // not interrupt productive work for TownRun only to have
        // BuildActionRequest immediately turn it back into Idle. Explicitly
        // blocked plans remain executable because their terminal outcome owns
        // the appropriate bounded backoff.
        if (newGoal == BotGoal::TownRun &&
            !Town::ShouldExecute(PreviewTownPlan()))
        {
            return;
        }

        if (_goal != newGoal)
        {
            bool interruptedRunningAction = false;

            // Non-interruptible active actions can only be interrupted by emergency goals
            if (_activeAction && !_activeAction->IsComplete() && !_activeAction->IsInterruptible())
            {
                if (newGoal != BotGoal::Combat && newGoal != BotGoal::Flee &&
                    newGoal != BotGoal::Resurrect && newGoal != BotGoal::Unstuck &&
                    newGoal != BotGoal::WaitForPartyResurrection)
                {
                    if (bot && Diagnostics::BotTrace::ShouldLog(bot))
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' Goal change {} -> {} REJECTED: current action {} is non-interruptible",
                            bot->GetName(), BotGoalToString(_goal), BotGoalToString(newGoal), _activeAction->GetName());
                    }
                    return;
                }
            }

            if (_activeAction && !_activeAction->IsComplete() &&
                _activeAction->GetOutcome() == Actions::ActionOutcome::Running)
            {
                _activeAction->OnInterrupted();
                _lastActionName = _activeAction->GetName();
                _lastActionOutcome = ActionOutcomeToString(
                    _activeAction->GetOutcome());
                _lastActionOutcomeReason = _activeAction->GetOutcomeReason();
                interruptedRunningAction = true;
                _transitionMetrics.interruptedActions++;
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::ActionsInterrupted);
                if (bot && Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' Action '{}' interrupted while Running by goal change to {}",
                        bot->GetName(), _activeAction->GetName(), BotGoalToString(newGoal));
                }
            }

            bool churnDetected = _transitionMetrics.RecordTransition(
                newGoal, getMSTime(), interruptedRunningAction);
            if (churnDetected && bot)
            {
                std::string recentGoalSequence;
                uint32_t sampleCount = std::min<uint32_t>(
                    _transitionMetrics.recentTransitions, 6);
                uint32_t start = (_transitionMetrics.goalIdx + 6 - sampleCount) % 6;
                for (uint32_t i = 0; i < sampleCount; ++i)
                {
                    if (!recentGoalSequence.empty())
                        recentGoalSequence += " -> ";
                    recentGoalSequence += BotGoalToString(
                        _transitionMetrics.recentGoals[(start + i) % 6]);
                }
                Diagnostics::SoakDigest::Record(static_cast<uint32_t>(bot->GetGUID().GetCounter()), Diagnostics::SoakEvent::GoalChurns);
                TC_LOG_WARN("server", "[WorldBots] [Brain] Bot '{}' CHURN DETECTED: {} running actions interrupted across {} goal transitions in {}s; recent goals: {}",
                    bot->GetName(), _transitionMetrics.recentInterruptedTransitions,
                    _transitionMetrics.recentTransitions, TransitionMetrics::CHURN_WINDOW_MS / 1000,
                    recentGoalSequence);
            }

            if (bot)
            {
                Diagnostics::StructuredEvent event;
                event.event = "goal_changed";
                event.goal = BotGoalToString(newGoal);
                event.action = _activeAction ? _activeAction->GetName() : "";
                event.actionInstance = _activeActionInstanceId;
                event.questId = _activeQuestId;
                AddMovementEvidence(event, _movement);
                event.details = std::string("previous_goal=") +
                    BotGoalToString(_goal) + ";interrupted=" +
                    (interruptedRunningAction ? "1" : "0");
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            }

            // Hostile context survives emergency transitions so a later death
            // can be attributed correctly. Ordinary goal changes must not
            // leave stale combat context behind.
            if (newGoal != BotGoal::Combat && newGoal != BotGoal::Flee &&
                newGoal != BotGoal::Grind && newGoal != BotGoal::Resurrect)
                _hostileContext.ClearHostile();
            if (newGoal != BotGoal::Combat && newGoal != BotGoal::Flee &&
                newGoal != BotGoal::Resurrect)
                _hostileContext.ClearProactiveRoute();

            if (bot && Diagnostics::BotTrace::ShouldLog(bot))
            {
                if (newGoal == BotGoal::TownRun)
                {
                    Town::Plan plan = PreviewTownPlan();
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) Goal changed: {} -> {} [tier: {}] (Reason: {}, FreeBagSlots: {}/{}, HasSellableItems: {}, NeedsRepair: {}) (AvailableQuests: {}, ActiveQuests: {}, CompletedQuests: {})",
                        bot->GetName(), bot->GetGUID().GetCounter(), BotGoalToString(_goal),
                        BotGoalToString(newGoal), GoalTierToString(_activeTier),
                        Town::DescribePrimaryPurpose(plan),
                        _blackboard.inv.freeBagSlots, _blackboard.inv.totalBagSlots,
                        _blackboard.inv.hasItemsToSell ? "Yes" : "No",
                        _blackboard.inv.needsRepair ? "Yes" : "No",
                        _blackboard.quest.availableQuests.size(), _blackboard.quest.activeQuests.size(), _blackboard.quest.completedQuests.size());
                }
                else
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) Goal changed: {} -> {} [tier: {}] (AvailableQuests: {}, ActiveQuests: {}, CompletedQuests: {})",
                        bot->GetName(), bot->GetGUID().GetCounter(), BotGoalToString(_goal), BotGoalToString(newGoal),
                        GoalTierToString(_activeTier),
                        _blackboard.quest.availableQuests.size(), _blackboard.quest.activeQuests.size(), _blackboard.quest.completedQuests.size());
                }
            }

            _goal = newGoal;
            if (newGoal != BotGoal::Flee)
                _fleeThreatGuid.Clear();
            SetAction(nullptr);
        }
    }

    void BotBrain::SetAction(std::unique_ptr<Actions::BotAction> newAction)
    {
        Player* bot = ResolveBot();
        if (_activeAction)
        {
            if (bot && _movement)
            {
                if (auto* wander = dynamic_cast<Actions::WanderAction*>(
                    _activeAction.get()))
                {
                    std::size_t persisted =
                        _suppressions.PersistWanderDestinationSuppressions(
                            wander->GetSuppressedDestinations(),
                            Helper::MonotonicSeconds());
                    if (persisted != 0)
                    {
                        Diagnostics::StructuredEvent event;
                        event.event = "wander_destination_suppressions_persisted";
                        event.goal = BotGoalToString(_goal);
                        event.action = _activeAction->GetName();
                        event.actionInstance = _activeActionInstanceId;
                        std::ostringstream details;
                        details << "changed_destination_suppressions=" << persisted;
                        event.details = details.str();
                        AddMovementEvidence(event, _movement);
                        Diagnostics::StructuredEventLog::Write(bot,
                            std::move(event));
                    }
                }
            }
            _activeAction->Stop(bot, _movement);
            if (bot && bot->IsNonMeleeSpellCast(false))
                bot->InterruptNonMeleeSpells(true);
        }

        _activeAction = std::move(newAction);
        _activeActionInstanceId = _activeAction
            ? ++_actionInstanceSequence : 0;
        _activeActionPathAttemptGeneration = _movement
            ? _movement->GetPathAttemptGeneration() : 0;
        _activeActionElapsedMs = 0;
        _activeActionIdleMs = 0;

        if (_activeAction && bot && _movement)
        {
            _movement->SetDiagnosticContext(BotGoalToString(_goal),
                _activeAction->GetName(), _activeActionInstanceId,
                _activeAction->GetRelatedQuestId() != 0
                    ? _activeAction->GetRelatedQuestId() : _activeQuestId);
            _activeAction->Start(bot, _movement);
            Diagnostics::StructuredEvent event;
            event.event = "action_started";
            event.goal = BotGoalToString(_goal);
            event.action = _activeAction->GetName();
            event.actionInstance = _activeActionInstanceId;
            event.questId = _activeAction->GetRelatedQuestId();
            AddMovementEvidence(event, _movement);
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
        }
    }
}
