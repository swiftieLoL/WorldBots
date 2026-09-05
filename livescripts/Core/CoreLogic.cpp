#include "Globals/ObjectMgr.h"
#include "CoreLogic.h"
#include "Auth/BotAuth.h"
#include "Auth/BotLoginPolicy.h"
#include "Factory/BotFactory.h"
#include "Config/BotConfig.h"
#include "Helper/MovementManager.h"
#include "Helper/InventoryUtils.h"
#include "Helper/TeleportUtils.h"
#include "Helper/BotMaintenance.h"
#include "Actions/LootAction.h"
#include "Actions/QuestAction.h"
#include "Actions/UnstuckAction.h"
#include "Sense/SenseCoordinator.h"
#include "Scheduler/Scheduler.h"
#include "Brain/BotBrain.h"
#include "Brain/GoalTier.h"
#include "Cache/BotCache.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/ProgressMonitor.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Diagnostics/SoakDigest.h"
#include "Commands/BotDiagnostics.h"
#include "Commands/BotCommands.h"
#include "ObjectAccessor.h"
#include "Cache/CharacterCache.h"
#include "World.h"
#include "Log.h"
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <limits>

struct BotRuntime
{
    std::unique_ptr<MovementManager> movement;
    std::unique_ptr<Brain::BotBrain> brain;
    uint64_t lastSenseClockMs = 0;
    uint64_t lastThinkClockMs = 0;
    uint64_t lastActionClockMs = 0;
    uint8_t lastLearnedLevel = 0;
    uint8_t lastProgressionLevel = 0;
    bool hunterPetProvisionAttempted = false;
};

struct RuntimeTaskMetrics
{
    uint64_t deferredBots = 0;
    uint64_t lastMicros = 0;
    uint64_t maxMicros = 0;
    uint32_t lastBatch = 0;
};

static std::unordered_map<ObjectGuid, BotRuntime> s_botRuntimes;
static std::unordered_map<ObjectGuid, std::chrono::steady_clock::time_point> s_invalidBotSince;
static std::unordered_map<ObjectGuid, uint32_t> s_lifecycleRecoveryAttempts;
static std::unordered_map<ObjectGuid, std::chrono::steady_clock::time_point> s_lastBotSave;
static std::deque<ObjectGuid> s_saveQueue;
static std::vector<ObjectGuid> s_runtimeOrder;
static size_t s_senseCursor = 0;
static size_t s_thinkCursor = 0;
static size_t s_actionCursor = 0;
static size_t s_maintenanceCursor = 0;
static uint64_t s_senseClockMs = 0;
static uint64_t s_thinkClockMs = 0;
static uint64_t s_actionClockMs = 0;
static RuntimeTaskMetrics s_senseMetrics;
static RuntimeTaskMetrics s_thinkMetrics;
static RuntimeTaskMetrics s_actionMetrics;
static RuntimeTaskMetrics s_maintenanceMetrics;
static constexpr std::chrono::seconds BotLifecycleGracePeriod{ 30 };
static bool s_debugMode = false;
static bool s_verboseLogging = false;
static bool s_runtimeEnabled = false;

static Framework::Scheduler s_scheduler;

static Diagnostics::LogMode ResolveLoggingMode(bool verboseOverride)
{
    if (verboseOverride)
        return Diagnostics::LogMode::Verbose;

    std::string mode = Config::BotConfig::GetLoggingMode();
    if (mode == "normal")
        return Diagnostics::LogMode::Normal;
    if (mode == "verbose")
        return Diagnostics::LogMode::Verbose;
    return Diagnostics::LogMode::Important;
}

static std::string EmitBotStatus(std::string status)
{
    TC_LOG_INFO("server", "[WorldBots] [Status]\n{}", status);
    return status;
}

static uint32_t ConsumeElapsed(uint64_t clockMs, uint64_t& previousClockMs)
{
    uint64_t elapsed = clockMs >= previousClockMs ? clockMs - previousClockMs : 0;
    previousClockMs = clockMs;
    return static_cast<uint32_t>(std::min<uint64_t>(elapsed, std::numeric_limits<uint32_t>::max()));
}

template <typename Callback>
static void ProcessRuntimeBatch(size_t& cursor, RuntimeTaskMetrics& metrics, Callback&& callback)
{
    // Runtime callbacks can remove or reorder bots. Iterate a stable GUID
    // snapshot so those mutations cannot invalidate the cursor or skip the
    // element that shifted into the current slot.
    const std::vector<ObjectGuid> runtimeOrderSnapshot = s_runtimeOrder;
    const size_t total = runtimeOrderSnapshot.size();
    if (total == 0)
    {
        cursor = 0;
        return;
    }

    cursor %= total;
    const size_t batchLimit = std::min<size_t>(Config::BotConfig::GetRuntimeBotBatchSize(), total);
    const auto started = std::chrono::steady_clock::now();
    const auto budget = std::chrono::milliseconds(Config::BotConfig::GetRuntimeTaskBudgetMs());
    uint32_t processed = 0;
    size_t inspected = 0;

    while (processed < batchLimit && inspected < total)
    {
        cursor %= total;
        ObjectGuid guid = runtimeOrderSnapshot[cursor];
        cursor = (cursor + 1) % total;
        ++inspected;
        auto runtime = s_botRuntimes.find(guid);
        if (runtime != s_botRuntimes.end())
        {
            callback(guid, runtime->second);
            ++processed;
        }

        if (processed > 0 && std::chrono::steady_clock::now() - started >= budget)
            break;
    }

    uint64_t elapsedMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count());
    metrics.lastBatch = processed;
    metrics.lastMicros = elapsedMicros;
    metrics.maxMicros = std::max(metrics.maxMicros, elapsedMicros);
    if (processed < total)
        metrics.deferredBots += total - processed;
}


static void AdjustCursorOnErase(size_t& cursor, size_t erasedIndex, size_t newTotal)
{
    if (newTotal == 0)
        cursor = 0;
    else if (erasedIndex < cursor)
        cursor = (cursor - 1) % newTotal;
    else
        cursor %= newTotal;
}

static void StopBotRuntime(ObjectGuid guid)
{
    s_invalidBotSince.erase(guid);
    s_lastBotSave.erase(guid);
    s_lifecycleRecoveryAttempts.erase(guid);
    s_saveQueue.erase(std::remove(s_saveQueue.begin(), s_saveQueue.end(), guid), s_saveQueue.end());
    Actions::LootAction::ClearBotState(guid);
    Actions::AcceptQuestAction::ClearBotState(guid);
    Actions::UnstuckAction::ClearBotState(guid);
    Diagnostics::BotTrace::SetEnabled(static_cast<uint32_t>(guid.GetCounter()), false);
    Diagnostics::ProgressMonitor::Remove(static_cast<uint32_t>(guid.GetCounter()));
    Diagnostics::SoakDigest::Remove(static_cast<uint32_t>(guid.GetCounter()));

    auto orderIt = std::find(s_runtimeOrder.begin(), s_runtimeOrder.end(), guid);
    if (orderIt != s_runtimeOrder.end())
    {
        size_t erasedIndex = std::distance(s_runtimeOrder.begin(), orderIt);
        s_runtimeOrder.erase(orderIt);
        size_t newTotal = s_runtimeOrder.size();
        AdjustCursorOnErase(s_senseCursor, erasedIndex, newTotal);
        AdjustCursorOnErase(s_thinkCursor, erasedIndex, newTotal);
        AdjustCursorOnErase(s_actionCursor, erasedIndex, newTotal);
        AdjustCursorOnErase(s_maintenanceCursor, erasedIndex, newTotal);
    }

    auto runtimeIt = s_botRuntimes.find(guid);
    if (runtimeIt != s_botRuntimes.end())
    {
        BotRuntime& runtime = runtimeIt->second;
        if (runtime.brain)
            runtime.brain->Shutdown();
        if (runtime.movement)
            runtime.movement->Shutdown();
        s_botRuntimes.erase(runtimeIt);
    }

    BotAuth::RemoveBotSession(guid);
}

static void ShutdownRuntime()
{
    BotAuth::CancelPendingSessions();

    // Stop brains before movement managers because actions hold a non-owning
    // pointer to their manager.
    for (auto& [guid, runtime] : s_botRuntimes)
    {
        if (runtime.brain)
            runtime.brain->Shutdown();
        if (runtime.movement)
            runtime.movement->Shutdown();
    }

    // BotAuth is the session ownership boundary. It logs out and destroys
    // module-owned socketless sessions while merely detaching adopted sessions
    // that belong to the world/session manager.
    for (const auto& [guid, runtime] : s_botRuntimes)
        BotAuth::RemoveBotSession(guid);

    s_botRuntimes.clear();
    s_invalidBotSince.clear();
    s_lastBotSave.clear();
    s_saveQueue.clear();
    s_runtimeOrder.clear();
    Actions::LootAction::ClearAllState();
    Actions::AcceptQuestAction::ClearAllState();
    Actions::UnstuckAction::ClearAllState();
    Sense::SenseCoordinator::ClearSharedCaches();
    Diagnostics::ProgressMonitor::Reset();
    Diagnostics::StructuredEventLog::Reset();
    Diagnostics::SoakDigest::Clear();
    Diagnostics::BotTrace::CloseFileLog();
    s_senseCursor = s_thinkCursor = s_actionCursor = s_maintenanceCursor = 0;
    s_lifecycleRecoveryAttempts.clear();
    Diagnostics::BotTrace::Clear();
}

static void RegisterActiveBot(Player* botPlayer)
{
    if (!botPlayer || !botPlayer->IsInWorld())
        return;

    if (!Config::BotConfig::IsBotClassAllowed(botPlayer->GetClass()))
    {
        TC_LOG_WARN("server", "[WorldBots] [Lifecycle] Refusing to run disabled-class bot '{}' (class {}); the managed session will be closed.",
            botPlayer->GetName(), static_cast<uint32_t>(botPlayer->GetClass()));
        StopBotRuntime(botPlayer->GetGUID());
        return;
    }

    ObjectGuid guid = botPlayer->GetGUID();
    auto existing = s_botRuntimes.find(guid);
    if (existing != s_botRuntimes.end())
    {
        if (existing->second.brain && existing->second.brain->GetBotGuid() == guid)
            return;

        StopBotRuntime(guid);
    }

    // Existing managed characters receive the same starter capacity as newly
    // created bots. Only empty equipped bag slots are filled.
    Helper::InventoryUtils::EnsureStarterBags(botPlayer,
        Config::BotConfig::GetStarterBagItemId(),
        Config::BotConfig::GetStarterBagCount());

    auto manager = std::make_unique<MovementManager>(botPlayer);
    Factory::BotDefinition definition = Factory::BotFactory::GetBotDefinition(botPlayer->GetName());
    auto brain = std::make_unique<Brain::BotBrain>(botPlayer, manager.get(), definition.profile);

    s_botRuntimes.emplace(guid, BotRuntime{ std::move(manager), std::move(brain),
        s_senseClockMs, s_thinkClockMs, s_actionClockMs });
    s_runtimeOrder.push_back(guid);
    s_lastBotSave[guid] = std::chrono::steady_clock::now();
    s_saveQueue.push_back(guid);
}

static MovementManager* GetBotMovementManager(uint32_t botGuidLow)
{
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(botGuidLow);
    auto it = s_botRuntimes.find(guid);
    if (it != s_botRuntimes.end())
        return it->second.movement.get();
    return nullptr;
}

static void ProcessPendingAuthSessions(uint32_t diff)
{
    BotAuth::UpdatePendingSessions(diff,
        [](Player* botPlayer, WorldSession* /*sess*/)
        {
            if (!botPlayer || !botPlayer->IsInWorld()) return;

            if (Diagnostics::BotTrace::ShouldLog(botPlayer, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_INFO("server", "[WorldBots] [Core] Bot '{}' (GUID: {}) successfully logged into the world at Map {}!",
                    botPlayer->GetName(), botPlayer->GetGUID().GetCounter(), botPlayer->GetMapId());
            }

            RegisterActiveBot(botPlayer);
        },
        [](uint32_t accountId, ObjectGuid guid, uint32_t attempt, const char* reason)
        {
            Factory::BotFactory::QueueLoginRetry(accountId, guid, attempt, reason);
        });
}

static void PruneInvalidBots()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<ObjectGuid> recoveryGuids;

    for (const auto& [guid, runtime] : s_botRuntimes)
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        BotAuth::SessionInfo sessionInfo = BotAuth::GetBotSessionInfo(guid);
        WorldSession* trackedSession = sessionInfo.session;
        BotAuth::SessionOwnership ownership = sessionInfo.ownership;
        // Adopted sessions can be destroyed by their external owner. Do not
        // dereference their cached raw session pointer after the player has
        // disappeared from ObjectAccessor.
        Player* sessionPlayer = ownership == BotAuth::SessionOwnership::Owned && trackedSession
            ? trackedSession->GetPlayer() : bot;
        auto invalidIt = s_invalidBotSince.find(guid);
        // Actions normally complete their own server-side teleport. Make one
        // lifecycle recovery attempt when a pending transfer is first seen,
        // but never retry it on every Sense/Think/Action scheduler pass.
        if (invalidIt == s_invalidBotSince.end() && sessionPlayer && sessionPlayer->IsBeingTeleported())
        {
            Helper::TeleportUtils::CompletePendingTeleport(sessionPlayer);
            bot = ObjectAccessor::FindPlayer(guid);
        }
        bool valid = runtime.brain && runtime.movement && bot && bot->GetSession() &&
            bot->IsInWorld() && !bot->IsBeingTeleported();

        if (valid)
        {
            if (invalidIt != s_invalidBotSince.end())
            {
                s_invalidBotSince.erase(invalidIt);
                s_lifecycleRecoveryAttempts.erase(guid);
                if (Diagnostics::BotTrace::ShouldLogGuid(static_cast<uint32_t>(guid.GetCounter()),
                    Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Lifecycle] Bot GUID {} returned to world; lifecycle grace cleared",
                        guid.GetCounter());
                }
            }
            continue;
        }

        if (invalidIt == s_invalidBotSince.end())
        {
            s_invalidBotSince.emplace(guid, now);
            if (Diagnostics::BotTrace::ShouldLogGuid(static_cast<uint32_t>(guid.GetCounter()),
                Diagnostics::LogEvent::Normal))
            {
                TC_LOG_WARN("server", "[WorldBots] [Lifecycle] Bot GUID {} became temporarily unavailable (Accessor: {}, Session: {}, InWorld: {}, Teleporting: {}); allowing {} seconds for repop/teleport completion",
                    guid.GetCounter(), bot ? "Present" : "Missing", trackedSession ? "Present" : "Missing",
                    sessionPlayer && sessionPlayer->IsInWorld() ? "Yes" : "No",
                    sessionPlayer && sessionPlayer->IsBeingTeleported() ? "Yes" : "No",
                    BotLifecycleGracePeriod.count());
            }
            continue;
        }

        if (now - invalidIt->second >= BotLifecycleGracePeriod)
            recoveryGuids.push_back(guid);
    }

    for (const ObjectGuid& guid : recoveryGuids)
    {
        BotAuth::SessionOwnership ownership = BotAuth::GetSessionOwnership(guid);
        if (ownership == BotAuth::SessionOwnership::Adopted)
        {
            TC_LOG_WARN("server", "[WorldBots] [Lifecycle] Adopted bot GUID {} did not return during lifecycle grace; detaching its runtime without creating a competing session",
                guid.GetCounter());
            StopBotRuntime(guid);
            s_lifecycleRecoveryAttempts.erase(guid);
            continue;
        }

        uint32_t& attempts = s_lifecycleRecoveryAttempts[guid];
        uint32_t maxRetries = Config::BotConfig::GetLoginMaxRetries();
        if (attempts >= maxRetries)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Lifecycle] Bot GUID {} exceeded max lifecycle recovery attempts ({}/{}); abandoning bot runtime.",
                guid.GetCounter(), attempts, maxRetries);
            s_lifecycleRecoveryAttempts.erase(guid);
            StopBotRuntime(guid);
            continue;
        }

        ++attempts;
        uint32_t delayMs = BotAuth::CalculateRetryDelayMs(attempts, 2000, 30000);
        uint32_t accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        if (accountId == 0)
            accountId = Config::BotConfig::GetBotAccountId();
        TC_LOG_ERROR("server", "[WorldBots] [Lifecycle] Bot GUID {} did not return during lifecycle grace (attempt {}/{}); rebuilding session with {}ms delay",
            guid.GetCounter(), attempts, maxRetries, delayMs);
        StopBotRuntime(guid);
        Factory::BotFactory::QueueBotLogin(accountId, guid, delayMs, attempts);
    }
}

static void ProcessSenseUpdates(uint32_t deltaMs)
{
    s_senseClockMs += deltaMs;
    ProcessRuntimeBatch(s_senseCursor, s_senseMetrics, [](ObjectGuid /*guid*/, BotRuntime& runtime) {
        if (runtime.brain)
            runtime.brain->Sense(ConsumeElapsed(s_senseClockMs, runtime.lastSenseClockMs));
    });
}

static void ProcessThinkUpdates(uint32_t deltaMs)
{
    s_thinkClockMs += deltaMs;
    ProcessRuntimeBatch(s_thinkCursor, s_thinkMetrics, [](ObjectGuid /*guid*/, BotRuntime& runtime) {
        if (runtime.brain)
            runtime.brain->Think(ConsumeElapsed(s_thinkClockMs, runtime.lastThinkClockMs));
    });
}

static void ProcessActionUpdates(uint32_t deltaMs)
{
    s_actionClockMs += deltaMs;
    ProcessRuntimeBatch(s_actionCursor, s_actionMetrics, [](ObjectGuid guid, BotRuntime& runtime) {
        uint32_t elapsed = ConsumeElapsed(s_actionClockMs, runtime.lastActionClockMs);
        if (runtime.brain)
            runtime.brain->UpdateAction(elapsed);
        auto it = s_botRuntimes.find(guid);
        if (it != s_botRuntimes.end() && it->second.movement)
            it->second.movement->Update(elapsed);
    });
}

static void ProcessMaintenanceUpdates(uint32_t)
{
    ProcessRuntimeBatch(s_maintenanceCursor, s_maintenanceMetrics, [](ObjectGuid /*guid*/, BotRuntime& runtime) {
        Player* bot = runtime.brain ? runtime.brain->GetBot() : nullptr;
        Helper::BotMaintenance::Update(bot, runtime.lastLearnedLevel,
            runtime.lastProgressionLevel, runtime.hunterPetProvisionAttempted);
    });
}

static void ProcessDebugPositionLogging(uint32_t)
{
    if (s_debugMode && !s_botRuntimes.empty())
    {
        for (const auto& [guid, runtime] : s_botRuntimes)
        {
            Player* botPlayer = ObjectAccessor::FindPlayer(guid);
            if (botPlayer && botPlayer->IsInWorld() && Diagnostics::BotTrace::ShouldLog(botPlayer))
            {
                TC_LOG_INFO("server", "[WorldBots] [Core] Debug Position: Bot '{}' (GUID: {}) Map {} at ({:.2f}, {:.2f}, {:.2f})",
                    botPlayer->GetName(), botPlayer->GetGUID().GetCounter(), botPlayer->GetMapId(),
                    botPlayer->GetPositionX(), botPlayer->GetPositionY(), botPlayer->GetPositionZ());
            }
        }
    }
}

static void ProcessProgressDiagnostics()
{
    if (!Diagnostics::ProgressMonitor::IsEnabled())
        return;

    std::vector<Diagnostics::ProgressSample> samples;
    samples.reserve(s_botRuntimes.size());
    for (const auto& [guid, runtime] : s_botRuntimes)
    {
        Player* bot = runtime.brain ? runtime.brain->GetBot() : nullptr;
        if (!bot || !bot->IsInWorld() || !runtime.movement)
            continue;

        const Brain::BotBrain& brain = *runtime.brain;
        const Blackboard::BotBlackboard& bb = brain.GetBlackboard();
        const Brain::TransitionMetrics& transitions = brain.GetTransitionMetrics();
        Diagnostics::ProgressSample sample;
        sample.guidLow = static_cast<uint32_t>(guid.GetCounter());
        sample.name = bot->GetName();
        sample.level = bot->GetLevel();
        sample.xp = bot->GetUInt32Value(PLAYER_XP);
        sample.nextLevelXp = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        sample.profile = brain.GetBehaviorProfileName();
        sample.goal = brain.GetGoalString();
        sample.tier = Brain::GoalTierToString(brain.GetActiveTier());
        sample.action = brain.GetActionString();
        sample.outcome = brain.GetActionOutcomeReason();
        sample.actionDetail = brain.GetActionDiagnosticDetail();
        sample.actionElapsedMs = brain.GetActiveActionElapsedMs();
        sample.actionIdleMs = brain.GetActiveActionIdleMs();
        sample.travelMode = brain.GetWorldTravelModeName();
        sample.travelWaitReason = brain.GetWorldTravelWaitReasonName();
        sample.casting = bot->IsNonMeleeSpellCast(false);
        sample.travelElapsedMs = brain.GetWorldTravelElapsedMs();
        sample.travelStepElapsedMs = brain.GetWorldTravelStepElapsedMs();
        sample.travelReplans = brain.GetWorldTravelReplanCount();
        sample.travelStepIndex = brain.GetWorldTravelStepIndex();
        sample.travelStepCount = brain.GetWorldTravelStepCount();
        sample.activeQuestId = brain.GetActiveQuestId();
        sample.watchdogQuestId = brain.GetQuestProgressWatchId();
        sample.watchdogMs = brain.GetQuestProgressWatchElapsedMs();
        sample.deathRecoverySeconds = brain.GetDeathRecoveryRemainingSeconds();
        sample.mapId = bot->GetMapId();
        sample.zoneId = bot->GetZoneId();
        sample.areaId = bot->GetAreaId();
        sample.x = bot->GetPositionX();
        sample.y = bot->GetPositionY();
        sample.z = bot->GetPositionZ();
        sample.healthPct = bb.self.healthPct;
        sample.manaPct = bb.self.manaPct;
        sample.dead = !bot->IsAlive();
        sample.inCombat = bot->IsInCombat();
        sample.snapshotReady = bb.initialSnapshotReady;
        sample.movement = runtime.movement->GetStateName();
        sample.hasPath = runtime.movement->HasPath();
        sample.pathFailure = runtime.movement->GetLastPathFailureName();
        sample.pathFlags = runtime.movement->GetLastPathFlags();
        sample.pathAttemptGeneration = runtime.movement->GetPathAttemptGeneration();
        sample.pathEvidenceFresh = brain.HasFreshActionPathEvidence();
        sample.originPathFailures = runtime.movement->GetOriginPathFailureCount();
        sample.originPathDestinations =
            runtime.movement->GetOriginPathFailureDestinationCount();
        sample.originRecoveryRequired = runtime.movement->NeedsOriginPathRecovery();
        sample.pathRequestX = runtime.movement->GetDestinationX();
        sample.pathRequestY = runtime.movement->GetDestinationY();
        sample.pathRequestZ = runtime.movement->GetDestinationZ();
        sample.pathEndpointAvailable = runtime.movement->GetLastPathAttemptEndpoint(
            sample.pathEndpointX, sample.pathEndpointY, sample.pathEndpointZ);
        sample.externalControl = runtime.movement->GetExternalControlModeName();
        sample.navStuck = bb.nav.isStuck;
        sample.freeBagSlots = bb.inv.freeBagSlots;
        sample.totalBagSlots = bb.inv.totalBagSlots;
        sample.bagsFull = bb.inv.bagsFull;
        sample.needsRepair = bb.inv.needsRepair;
        sample.needsRestock = bb.inv.needsRestock;
        sample.availableQuests = static_cast<uint32_t>(bb.quest.availableQuests.size());
        sample.activeQuests = static_cast<uint32_t>(bb.quest.activeQuests.size());
        sample.completedQuests = static_cast<uint32_t>(bb.quest.completedQuests.size());
        sample.totalTransitions = transitions.totalTransitions;
        sample.recentInterruptedTransitions = transitions.recentInterruptedTransitions;
        sample.churning = transitions.IsChurning();
        for (size_t i = 0; i < sample.soakTotals.size(); ++i)
        {
            sample.soakTotals[i] = Diagnostics::SoakDigest::GetTotal(
                sample.guidLow, static_cast<Diagnostics::SoakEvent>(i));
        }
        samples.push_back(std::move(sample));
    }

    // Include configured slots that are absent from the runtime. Without
    // these rows, a bot stuck in provisioning, login retry, or lifecycle
    // recovery would disappear from the live file and look deceptively quiet.
    for (uint32_t slot = 0; slot < Config::BotConfig::GetBotCount(); ++slot)
    {
        std::string name = Factory::BotFactory::NormalizeBotName(
            Factory::BotFactory::GenerateBotName(slot));
        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name);
        if (guid && s_botRuntimes.find(guid) != s_botRuntimes.end())
            continue;

        Diagnostics::ProgressSample sample;
        sample.guidLow = guid ? static_cast<uint32_t>(guid.GetCounter()) : 0;
        sample.name = std::move(name);
        sample.runtimeActive = false;
        sample.level = guid ? sCharacterCache->GetCharacterLevelByGuid(guid) : 0;
        sample.profile = Factory::BehaviorProfileName(
            Factory::BotFactory::GetBotDefinition(sample.name).profile);
        sample.goal = "Unavailable";
        sample.tier = "Unavailable";
        sample.action = "Unavailable";
        sample.movement = "Unavailable";
        sample.pathFailure = "Unavailable";
        sample.externalControl = "None";
        samples.push_back(std::move(sample));
    }

    Diagnostics::ProgressMonitor::WriteSnapshot(std::move(samples));
}

static void ProcessSaveUpdates(uint32_t)
{
    if (!Config::BotConfig::ShouldSaveBotProgress() || s_saveQueue.empty())
        return;

    auto now = std::chrono::steady_clock::now();
    uint32_t remaining = Config::BotConfig::GetSaveBatchSize();
    size_t inspected = 0;
    size_t initialQueueSize = s_saveQueue.size();
    while (remaining > 0 && inspected < initialQueueSize && !s_saveQueue.empty())
    {
        ++inspected;
        ObjectGuid guid = s_saveQueue.front();
        s_saveQueue.pop_front();

        auto runtime = s_botRuntimes.find(guid);
        auto lastSave = s_lastBotSave.find(guid);
        if (runtime == s_botRuntimes.end() || lastSave == s_lastBotSave.end())
            continue;

        if (now - lastSave->second < std::chrono::milliseconds(Config::BotConfig::GetSaveBotIntervalMs()))
        {
            s_saveQueue.push_back(guid);
            continue;
        }

        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (bot && bot->IsInWorld() && !bot->IsBeingTeleported())
        {
            bot->SaveToDB();
            // Re-verify bot still exists in runtime after SaveToDB
            auto saveIt = s_lastBotSave.find(guid);
            if (saveIt != s_lastBotSave.end() && s_botRuntimes.find(guid) != s_botRuntimes.end())
            {
                saveIt->second = now;
                --remaining;
                s_saveQueue.push_back(guid);
            }
        }
        else
        {
            // Teleporting or transiently unavailable: requeue without resetting save timestamp
            if (s_botRuntimes.find(guid) != s_botRuntimes.end())
                s_saveQueue.push_back(guid);
        }
    }
}

static BotRuntime* FindRuntimeByName(const std::string& botName)
{
    std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
    if (!guid)
        return nullptr;
    auto runtime = s_botRuntimes.find(guid);
    return runtime != s_botRuntimes.end() ? &runtime->second : nullptr;
}

namespace Core
{
    bool CoreLogic::InitializeFromConfig()
    {
        if (!Config::BotConfig::LoadModuleRuntimeConfig())
        {
            ShutdownRuntime();
            s_scheduler.Clear();
            s_runtimeEnabled = false;
            TC_LOG_ERROR("server", "[WorldBots] [Core] WorldBots initialization stopped because its module-local configuration could not be loaded.");
            return false;
        }

        if (!Config::BotConfig::IsEnabled())
        {
            ShutdownRuntime();
            s_scheduler.Clear();
            s_runtimeEnabled = false;
            TC_LOG_INFO("server", "[WorldBots] [Core] WorldBots is disabled by configuration.");
            return false;
        }

        InitializeBotFactory(
            Config::BotConfig::GetBotCount(),
            Config::BotConfig::IsDebugModeEnabled(),
            false);
        return true;
    }

    void CoreLogic::InitializeBotFactory(uint32_t botCount, bool debugMode, bool verboseLogging)
    {
        s_debugMode = debugMode;
        Diagnostics::BotTrace::SetMode(ResolveLoggingMode(verboseLogging));
        s_verboseLogging = Diagnostics::BotTrace::IsGlobalVerbose();

        Cache::BotCache::Initialize();

        ShutdownRuntime();
        s_scheduler.Clear();
        Diagnostics::SoakDigest::Clear();
        Diagnostics::ProgressMonitor::Configure(
            Config::BotConfig::IsProgressDiagnosticsEnabled(),
            Config::BotConfig::GetProgressDiagnosticsDirectory(),
            Config::BotConfig::GetProgressDiagnosticsStallSeconds());
        Diagnostics::StructuredEventLog::Configure(
            Config::BotConfig::IsStructuredEventDiagnosticsEnabled(),
            Config::BotConfig::GetProgressDiagnosticsDirectory(),
            Config::BotConfig::GetStructuredEventDiagnosticBots());
        std::string fileLogLevel = Config::BotConfig::GetFileTraceLoggingLevel();
        Diagnostics::LogMode fileMode = Diagnostics::LogMode::Verbose;
        if (fileLogLevel == "important")
            fileMode = Diagnostics::LogMode::Important;
        else if (fileLogLevel == "normal")
            fileMode = Diagnostics::LogMode::Normal;
        Diagnostics::BotTrace::ConfigureFileLog(
            Config::BotConfig::IsFileTraceLoggingEnabled(),
            Config::BotConfig::GetProgressDiagnosticsDirectory(),
            fileMode,
            Config::BotConfig::GetFileTraceLoggingBots());
        s_senseClockMs = s_thinkClockMs = s_actionClockMs = 0;
        s_senseMetrics = {};
        s_thinkMetrics = {};
        s_actionMetrics = {};
        s_maintenanceMetrics = {};

        // SpawnTask: 100ms interval (Lifecycle & Auth query processing)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "SpawnTask", 100, [](uint32_t deltaMs) {
                Factory::BotFactory::ProcessDeferredSpawns(deltaMs);
                ProcessPendingAuthSessions(deltaMs);
            }
        ));

        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "LifecycleTask", 500, [](uint32_t) {
                PruneInvalidBots();
            }
        ));

        s_runtimeEnabled = true;

        // SenseTask: 50ms resolution (Perception substate timers service)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "SenseTask", 50, [](uint32_t deltaMs) {
                ProcessSenseUpdates(deltaMs);
            }
        ));

        // ThinkTask: 500ms interval (Goal evaluation & Action selection)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "ThinkTask", 500, [](uint32_t deltaMs) {
                ProcessThinkUpdates(deltaMs);
            }
        ));

        // ActionTask: 50ms interval (Action tick execution & synchronized movement pipeline)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "ActionTask", 50, [](uint32_t deltaMs) {
                ProcessActionUpdates(deltaMs);
            }
        ));

        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "MaintenanceTask", 1000, [](uint32_t deltaMs) {
                ProcessMaintenanceUpdates(deltaMs);
            }
        ));

        // DebugPositionTask: 2000ms interval (Position logging)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "DebugPositionTask", 2000, [](uint32_t deltaMs) {
                ProcessDebugPositionLogging(deltaMs);
            }
        ));

        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "SaveTask", Config::BotConfig::GetSaveBatchIntervalMs(), [](uint32_t deltaMs) {
                ProcessSaveUpdates(deltaMs);
            }
        ));

        // SoakDigest: 30-minute interval (periodic edge case summary)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "SoakDigestTask", 1800000, [](uint32_t) {
                Diagnostics::SoakDigest::EmitDigest();
            }
        ));

        if (Diagnostics::ProgressMonitor::IsEnabled())
        {
            s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
                "ProgressDiagnosticsTask", Config::BotConfig::GetProgressDiagnosticsIntervalMs(), [](uint32_t) {
                    ProcessProgressDiagnostics();
                }
            ));
        }

        if (Diagnostics::BotTrace::ShouldLog(nullptr, Diagnostics::LogEvent::Normal))
        {
            TC_LOG_INFO("server", "[WorldBots] [Core] BotFactory initialized (BotCount: {}, Logging: {}, PositionLogging: {})",
                botCount, Diagnostics::BotTrace::GetModeName(), debugMode ? 1 : 0);
        }

        // Delegate bot character creation and cleanup to BotFactory
        Factory::BotFactory::InitializeBots(botCount, verboseLogging);
    }

    std::string CoreLogic::GetBotStatus(std::string const& botName)
    {
        BotRuntime* runtime = FindRuntimeByName(botName);
        if (!runtime)
            return EmitBotStatus("Bot '" + botName + "' not found in character cache.");

        if (!runtime->brain)
            return EmitBotStatus("Bot '" + botName + "' has no active brain or is not in world.");

        Player* bot = runtime->brain->GetBot();
        if (!bot || !bot->IsInWorld())
        {
            std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
            auto lifecycle = s_invalidBotSince.find(guid);
            if (lifecycle != s_invalidBotSince.end())
            {
                auto elapsed = std::chrono::steady_clock::now() - lifecycle->second;
                auto remaining = elapsed < BotLifecycleGracePeriod
                    ? std::chrono::duration_cast<std::chrono::seconds>(BotLifecycleGracePeriod - elapsed).count() : 0;
                return EmitBotStatus(fmt::format(
                    "[WorldBots Status] {}\n - Lifecycle: Temporarily unavailable during repop/teleport\n - Brain: Preserved\n - Recovery grace remaining: {}s",
                    formattedName, remaining));
            }
            return EmitBotStatus("Bot '" + botName + "' is not currently in world.");
        }
        return Commands::BotDiagnostics::FormatBotStatus(runtime->brain.get(), runtime->movement.get());
    }

    static Brain::BotBrain* FindBotBrainByName(std::string const& botName)
    {
        BotRuntime* runtime = FindRuntimeByName(botName);
        return runtime ? runtime->brain.get() : nullptr;
    }

    std::string CoreLogic::GetBotVendorStatus(std::string const& botName)
    {
        Brain::BotBrain* brain = FindBotBrainByName(botName);
        if (!brain)
            return EmitBotStatus("Bot '" + botName + "' has no active brain or is not in world.");
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld())
            return EmitBotStatus("Bot '" + botName + "' is not currently in world.");
        return Commands::BotDiagnostics::FormatVendorStatus(brain);
    }

    std::string CoreLogic::GetBotQuestStatus(std::string const& botName)
    {
        Brain::BotBrain* brain = FindBotBrainByName(botName);
        if (!brain)
            return EmitBotStatus("Bot '" + botName + "' has no active brain or is not in world.");
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld())
            return EmitBotStatus("Bot '" + botName + "' is not currently in world.");
        return Commands::BotDiagnostics::FormatQuestStatus(brain);
    }

    std::string CoreLogic::RunTestCommand(std::string const& arguments)
    {
        return Commands::BotCommands::RunTest(arguments, FindBotBrainByName);
    }

    std::string CoreLogic::RunTraceCommand(std::string const& arguments)
    {
        return Commands::BotCommands::RunTrace(arguments, FindBotBrainByName);
    }

    void CoreLogic::SetVerboseLogging(bool enabled)
    {
        s_verboseLogging = enabled;
        Diagnostics::BotTrace::SetGlobalVerbose(enabled);
    }

    bool CoreLogic::IsVerboseLoggingEnabled()
    {
        return s_verboseLogging;
    }

    void CoreLogic::Update(uint32_t diff)
    {
        if (s_runtimeEnabled)
            s_scheduler.Update(diff);
        else
            BotAuth::UpdatePendingSessions(diff, nullptr);
    }

    uint32_t CoreLogic::GetActiveBotCount()
    {
        return static_cast<uint32_t>(s_botRuntimes.size());
    }

    std::string CoreLogic::GetFactoryStatus()
    {
        return EmitBotStatus(fmt::format(
            "[WorldBots Factory]\n"
            " - Active bots: {}\n"
            " - Awaiting account/character preparation: {}\n"
            " - Prepared login queue (including retries): {}\n"
            " - Login pipelines in flight: {}/{}\n"
            " - Startup/player grace/launch cooldown: {} / {} / {} ms\n"
            " - Player login priority pause: {} (realm queue: {})\n"
            " - Sessions owned/adopted: {} / {}\n"
            " - Login timeout/retries: {} ms / {}\n"
            " - Save batch: {} every {} ms (per-bot target {} ms)\n"
            " - Account mode: {}\n"
            " - Factory operations/budget per tick: {} / {} ms\n"
            " - Runtime batch/budget: {} bots / {} ms\n"
            " - Sense last/max/deferred: {} bots in {} us / {} us / {}\n"
            " - Think last/max/deferred: {} bots in {} us / {} us / {}\n"
            " - Action last/max/deferred: {} bots in {} us / {} us / {}\n"
            " - Maintenance last/max/deferred: {} bots in {} us / {} us / {}\n"
            " - Logging: {} (traced bots receive detail in every mode)\n"
            " - Maximum bot count: {}",
            GetActiveBotCount(),
            Factory::BotFactory::GetPendingProvisionCount(),
            Factory::BotFactory::GetPendingSpawnCount(),
            BotAuth::GetPendingLoginCount(),
            Config::BotConfig::GetMaxConcurrentLogins(),
            Factory::BotFactory::GetStartupGraceRemainingMs(),
            Factory::BotFactory::GetPlayerLoginGraceRemainingMs(),
            Factory::BotFactory::GetLoginLaunchCooldownRemainingMs(),
            Factory::BotFactory::IsPausedForPlayerLogin() ? "Yes" : "No",
            sWorld ? sWorld->GetQueuedSessionCount() : 0,
            BotAuth::GetOwnedSessionCount(),
            BotAuth::GetAdoptedSessionCount(),
            Config::BotConfig::GetLoginTimeoutMs(),
            Config::BotConfig::GetLoginMaxRetries(),
            Config::BotConfig::GetSaveBatchSize(),
            Config::BotConfig::GetSaveBatchIntervalMs(),
            Config::BotConfig::GetSaveBotIntervalMs(),
            Config::BotConfig::UseDedicatedAccounts() ? "Dedicated (one marked account per bot)" : "Shared legacy account",
            Config::BotConfig::GetFactoryOperationsPerTick(),
            Config::BotConfig::GetFactoryTaskBudgetMs(),
            Config::BotConfig::GetRuntimeBotBatchSize(),
            Config::BotConfig::GetRuntimeTaskBudgetMs(),
            s_senseMetrics.lastBatch, s_senseMetrics.lastMicros, s_senseMetrics.maxMicros, s_senseMetrics.deferredBots,
            s_thinkMetrics.lastBatch, s_thinkMetrics.lastMicros, s_thinkMetrics.maxMicros, s_thinkMetrics.deferredBots,
            s_actionMetrics.lastBatch, s_actionMetrics.lastMicros, s_actionMetrics.maxMicros, s_actionMetrics.deferredBots,
            s_maintenanceMetrics.lastBatch, s_maintenanceMetrics.lastMicros, s_maintenanceMetrics.maxMicros, s_maintenanceMetrics.deferredBots,
            Diagnostics::BotTrace::GetModeName(),
            Config::BotConfig::GetMaxBotCount()));
    }

    bool CoreLogic::BotMoveTo(uint32_t botGuidLow, float x, float y, float z)
    {
        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            return mgr->MoveToExternal(x, y, z);
        }
        return false;
    }

    void CoreLogic::BotFollow(uint32_t botGuidLow, uint32_t targetGuidLow, float distance, float angle)
    {
        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            ObjectGuid targetGuid = ObjectGuid::Create<HighGuid::Player>(targetGuidLow);
            mgr->FollowExternal(targetGuid, distance, angle);
        }
    }

    void CoreLogic::BotChase(uint32_t botGuidLow, uint32_t targetGuidLow)
    {
        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            ObjectGuid targetGuid = ObjectGuid::Create<HighGuid::Player>(targetGuidLow);
            mgr->ChaseExternal(targetGuid);
        }
    }

    void CoreLogic::BotStop(uint32_t botGuidLow)
    {
        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            mgr->StopExternal();
        }
    }

    uint8_t CoreLogic::BotGetMovementState(uint32_t botGuidLow)
    {
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(botGuidLow);
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return static_cast<uint8_t>(BotMovementState::Idle);

        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            return static_cast<uint8_t>(mgr->GetState());
        }
        return static_cast<uint8_t>(BotMovementState::Idle);
    }
}
