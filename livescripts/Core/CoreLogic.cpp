#include "Globals/ObjectMgr.h"
#include "CoreLogic.h"
#include "Auth/BotAuth.h"
#include "Factory/BotFactory.h"
#include "Config/BotConfig.h"
#include "Helper/MovementManager.h"
#include "Helper/NpcFinder.h"
#include "Helper/InventoryUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/QuestUtils.h"
#include "Helper/TeleportUtils.h"
#include "Actions/LootAction.h"
#include "Scheduler/Scheduler.h"
#include "Brain/BotBrain.h"
#include "Cache/BotCache.h"
#include "Testing/ScenarioRunner.h"
#include "Diagnostics/BotTrace.h"
#include "ObjectAccessor.h"
#include "Globals/ObjectMgr.h"
#include "Item.h"
#include "Creature.h"
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
#include <cstdio>
#include <cctype>
#include <chrono>
#include <sstream>

struct BotRuntime
{
    std::unique_ptr<MovementManager> movement;
    std::unique_ptr<Brain::BotBrain> brain;
};

static std::unordered_map<ObjectGuid, BotRuntime> s_botRuntimes;
static std::unordered_map<ObjectGuid, std::chrono::steady_clock::time_point> s_invalidBotSince;
static std::unordered_map<ObjectGuid, std::chrono::steady_clock::time_point> s_lastBotSave;
static std::deque<ObjectGuid> s_saveQueue;
static constexpr std::chrono::seconds BotLifecycleGracePeriod{ 30 };
static bool s_debugMode = false;
static bool s_verboseLogging = false;
static bool s_runtimeEnabled = false;

static Framework::Scheduler s_scheduler;

static std::string EmitBotStatus(std::string status)
{
    TC_LOG_INFO("server", "[WorldBots] [Status]\n{}", status);
    return status;
}

static const char* QuestObjectiveTypeName(Blackboard::QuestObjectiveType type)
{
    switch (type)
    {
        case Blackboard::QuestObjectiveType::KillCreature: return "KillCreature";
        case Blackboard::QuestObjectiveType::CollectItem: return "CollectItem";
        case Blackboard::QuestObjectiveType::TalkToCreature: return "Creature";
        case Blackboard::QuestObjectiveType::CastOnCreature: return "CastOnCreature";
        case Blackboard::QuestObjectiveType::InteractGameObject: return "InteractGameObject";
        case Blackboard::QuestObjectiveType::Explore: return "Explore";
        case Blackboard::QuestObjectiveType::Unsupported: return "Unsupported";
        default: return "Unknown";
    }
}

static const char* InventoryResultName(InventoryResult result)
{
    switch (result)
    {
        case EQUIP_ERR_OK: return "OK";
        case EQUIP_ERR_INVENTORY_FULL: return "INVENTORY_FULL";
        case EQUIP_ERR_CANT_CARRY_MORE_OF_THIS: return "CANT_CARRY_MORE";
        case EQUIP_ERR_ITEM_CANT_STACK: return "ITEM_CANT_STACK";
        default: return "OTHER";
    }
}

static void StopBotRuntime(ObjectGuid guid)
{
    s_invalidBotSince.erase(guid);
    s_lastBotSave.erase(guid);
    s_saveQueue.erase(std::remove(s_saveQueue.begin(), s_saveQueue.end(), guid), s_saveQueue.end());
    Diagnostics::BotTrace::SetEnabled(static_cast<uint32_t>(guid.GetCounter()), false);
    auto runtimeIt = s_botRuntimes.find(guid);
    if (runtimeIt != s_botRuntimes.end())
    {
        BotRuntime& runtime = runtimeIt->second;
        if (runtime.brain)
            runtime.brain->Shutdown();
        if (runtime.movement)
            runtime.movement->Stop();
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
            runtime.movement->Stop();
    }

    for (const auto& [guid, runtime] : s_botRuntimes)
    {
        if (Player* bot = ObjectAccessor::FindPlayer(guid))
        {
            if (WorldSession* session = bot->GetSession())
                session->LogoutPlayer(true);
        }
        BotAuth::RemoveBotSession(guid);
    }

    s_botRuntimes.clear();
    s_invalidBotSince.clear();
    s_lastBotSave.clear();
    s_saveQueue.clear();
    Diagnostics::BotTrace::Clear();
}

static void RegisterActiveBot(Player* botPlayer)
{
    if (!botPlayer || !botPlayer->IsInWorld())
        return;

    ObjectGuid guid = botPlayer->GetGUID();
    auto existing = s_botRuntimes.find(guid);
    if (existing != s_botRuntimes.end())
    {
        if (existing->second.brain && existing->second.brain->GetBot() == botPlayer)
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

    s_botRuntimes.emplace(guid, BotRuntime{ std::move(manager), std::move(brain) });
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

            TC_LOG_INFO("server", "[WorldBots] [Core] Bot '{}' (GUID: {}) successfully logged into the world at Map {}!",
                botPlayer->GetName(), botPlayer->GetGUID().GetCounter(), botPlayer->GetMapId());

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
        WorldSession* trackedSession = BotAuth::GetBotSession(guid);
        Player* sessionPlayer = trackedSession ? trackedSession->GetPlayer() : nullptr;
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
                TC_LOG_INFO("server", "[WorldBots] [Lifecycle] Bot '{}' (GUID: {}) returned to the world during lifecycle grace; preserving its session and brain",
                    bot->GetName(), guid.GetCounter());
                s_invalidBotSince.erase(invalidIt);
            }
            continue;
        }

        if (invalidIt == s_invalidBotSince.end())
        {
            s_invalidBotSince.emplace(guid, now);
            TC_LOG_WARN("server", "[WorldBots] [Lifecycle] Bot GUID {} became temporarily unavailable (Accessor: {}, Session: {}, InWorld: {}, Teleporting: {}); allowing {} seconds for repop/teleport completion",
                guid.GetCounter(), bot ? "Present" : "Missing", trackedSession ? "Present" : "Missing",
                sessionPlayer && sessionPlayer->IsInWorld() ? "Yes" : "No",
                sessionPlayer && sessionPlayer->IsBeingTeleported() ? "Yes" : "No",
                BotLifecycleGracePeriod.count());
            continue;
        }

        if (now - invalidIt->second >= BotLifecycleGracePeriod)
            recoveryGuids.push_back(guid);
    }

    for (const ObjectGuid& guid : recoveryGuids)
    {
        uint32_t accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        if (accountId == 0)
            accountId = Config::BotConfig::GetBotAccountId();
        TC_LOG_ERROR("server", "[WorldBots] [Lifecycle] Bot GUID {} did not return during lifecycle grace; rebuilding its owned session and runtime",
            guid.GetCounter());
        StopBotRuntime(guid);
        Factory::BotFactory::QueueBotLogin(accountId, guid);
    }
}

static void ProcessSenseUpdates(uint32_t deltaMs)
{
    PruneInvalidBots();
    for (auto& [guid, runtime] : s_botRuntimes)
    {
        if (runtime.brain)
            runtime.brain->Sense(deltaMs);
    }
}

static void ProcessThinkUpdates(uint32_t deltaMs)
{
    PruneInvalidBots();
    for (auto& [guid, runtime] : s_botRuntimes)
    {
        if (runtime.brain)
            runtime.brain->Think(deltaMs);
    }
}

static void ProcessMovementUpdates(uint32_t diff)
{
    for (auto& [guid, runtime] : s_botRuntimes)
    {
        if (runtime.movement)
            runtime.movement->Update(diff);
    }
}

static void ProcessActionUpdates(uint32_t deltaMs)
{
    PruneInvalidBots();
    for (auto& [guid, runtime] : s_botRuntimes)
    {
        if (runtime.brain)
            runtime.brain->UpdateAction(deltaMs);
    }

    // Synchronized Movement Execution: Run movement manager update immediately after action tick
    ProcessMovementUpdates(deltaMs);
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

static void ProcessSaveUpdates(uint32_t)
{
    if (!Config::BotConfig::ShouldSaveBotProgress() || s_saveQueue.empty())
        return;

    auto now = std::chrono::steady_clock::now();
    uint32_t remaining = Config::BotConfig::GetSaveBatchSize();
    while (remaining-- > 0 && !s_saveQueue.empty())
    {
        ObjectGuid guid = s_saveQueue.front();
        s_saveQueue.pop_front();

        auto runtime = s_botRuntimes.find(guid);
        auto lastSave = s_lastBotSave.find(guid);
        if (runtime == s_botRuntimes.end() || lastSave == s_lastBotSave.end())
            continue;

        if (now - lastSave->second < std::chrono::milliseconds(Config::BotConfig::GetSaveBotIntervalMs()))
        {
            s_saveQueue.push_front(guid);
            break;
        }

        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (bot && bot->IsInWorld())
            bot->SaveToDB();

        lastSave->second = now;
        s_saveQueue.push_back(guid);
    }
}

namespace Core
{
    bool CoreLogic::InitializeFromConfig()
    {
        if (!Config::BotConfig::LoadModuleRuntimeConfig())
        {
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
            Config::BotConfig::IsVerboseLoggingEnabled());
        return true;
    }

    void CoreLogic::InitializeBotFactory(uint32_t botCount, bool debugMode, bool verboseLogging)
    {
        Cache::BotCache::Initialize();

        s_debugMode = debugMode;
        s_verboseLogging = verboseLogging;
        Diagnostics::BotTrace::SetGlobalVerbose(verboseLogging);

        ShutdownRuntime();
        s_scheduler.Clear();

        // SpawnTask: 100ms interval (Lifecycle & Auth query processing)
        s_scheduler.RegisterTask(std::make_shared<Framework::ScheduledTask>(
            "SpawnTask", 100, [](uint32_t deltaMs) {
                Factory::BotFactory::ProcessDeferredSpawns(deltaMs);
                ProcessPendingAuthSessions(deltaMs);
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

        if (s_verboseLogging)
        {
            TC_LOG_INFO("server", "[WorldBots] [Core] BotFactory initialized (BotCount: {}, DebugMode: {}, VerboseLogging: {})",
                botCount, debugMode ? 1 : 0, verboseLogging ? 1 : 0);
        }

        // Delegate bot character creation and cleanup to BotFactory
        Factory::BotFactory::InitializeBots(botCount, verboseLogging);
    }

    std::string CoreLogic::GetBotStatus(std::string const& botName)
    {
        std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);

        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
        if (!guid)
        {
            return EmitBotStatus("Bot '" + botName + "' not found in character cache.");
        }

        auto it = s_botRuntimes.find(guid);
        if (it == s_botRuntimes.end() || !it->second.brain)
        {
            return EmitBotStatus("Bot '" + botName + "' has no active brain or is not in world.");
        }

        Brain::BotBrain* brain = it->second.brain.get();
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld())
        {
            auto lifecycleIt = s_invalidBotSince.find(guid);
            if (lifecycleIt != s_invalidBotSince.end())
            {
                auto elapsed = std::chrono::steady_clock::now() - lifecycleIt->second;
                auto remaining = elapsed < BotLifecycleGracePeriod
                    ? std::chrono::duration_cast<std::chrono::seconds>(BotLifecycleGracePeriod - elapsed).count()
                    : 0;
                return EmitBotStatus(fmt::format(
                    "[WorldBots Status] {}\n - Lifecycle: Temporarily unavailable during repop/teleport\n - Brain: Preserved\n - Recovery grace remaining: {}s",
                    formattedName, remaining));
            }
            return EmitBotStatus("Bot '" + botName + "' is not currently in world.");
        }

        const auto& bb = brain->GetBlackboard();
        std::string goalStr = brain->GetGoalString();
        std::string actionStr = brain->GetActionString();
        std::string actionReason = brain->GetActionOutcomeReason();
        bool explicitlyTraced = Diagnostics::BotTrace::IsEnabled(
            static_cast<uint32_t>(bot->GetGUID().GetCounter()));
        const char* traceStatus = explicitlyTraced ? "Enabled" :
            (Diagnostics::BotTrace::IsGlobalVerbose() ? "Global verbose" : "Disabled");

        MovementManager* movement = it->second.movement.get();
        const char* movementState = movement ? movement->GetStateName() : "Unavailable";
        const char* externalMode = movement ? movement->GetExternalControlModeName() : "Unavailable";
        const char* hasPath = movement && movement->HasPath() ? "Yes" : "No";
        std::string destination = movement && movement->HasPath()
            ? fmt::format("({:.1f}, {:.1f}, {:.1f})", movement->GetDestinationX(),
                movement->GetDestinationY(), movement->GetDestinationZ())
            : "None";

        return EmitBotStatus(fmt::format(
            "[WorldBots Status] {} (Level {})\n"
            " - Profile: {}\n"
            " - Goal: {}\n"
            " - Action: {}\n"
            " - Quest Context: {}\n"
            " - Last Action Detail: {}\n"
            " - Trace: {}\n"
            " - Movement: {} | Path: {} | External Control: {}\n"
            " - Movement Destination: {}\n"
            " - Blackboard Quests: Available: {} | Active: {} | Completed: {}\n"
            " - Position: Map {} at ({:.1f}, {:.1f}, {:.1f})",
            bot->GetName(), bot->GetLevel(),
            brain->GetBehaviorProfileName(), goalStr, actionStr,
            brain->GetActiveQuestId() != 0 ? std::to_string(brain->GetActiveQuestId()) : "None",
            actionReason.empty() ? "Running / none" : actionReason,
            traceStatus,
            movementState, hasPath, externalMode,
            destination,
            bb.quest.availableQuests.size(), bb.quest.activeQuests.size(), bb.quest.completedQuests.size(),
            bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));
    }

    std::string CoreLogic::GetBotVendorStatus(std::string const& botName)
    {
        std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
        auto runtime = guid ? s_botRuntimes.find(guid) : s_botRuntimes.end();
        if (!guid || runtime == s_botRuntimes.end() || !runtime->second.brain)
            return EmitBotStatus("Bot '" + botName + "' has no active brain or is not in world.");

        Brain::BotBrain* brain = runtime->second.brain.get();
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld())
            return EmitBotStatus("Bot '" + botName + "' is not currently in world.");

        const auto& bb = brain->GetBlackboard();
        Town::Plan plan = brain->PreviewTownPlan();
        bool lowSpace = Helper::InventoryUtils::CountFreeBagSlots(bot) <= 3;
        Helper::InventoryPolicyContext inventoryPolicy =
            Helper::InventoryUtils::BuildPolicyContext(bot, lowSpace);
        uint32_t sellableStacks = 0;
        uint32_t discardableStacks = 0;
        uint32_t protectedStacks = 0;
        std::ostringstream items;

        Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item* item) {
            ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
            Helper::InventoryItemDecision decision =
                Helper::InventoryUtils::ClassifyForSpace(bot, item, inventoryPolicy);
            sellableStacks += decision.sell ? 1u : 0u;
            discardableStacks += decision.discardWhenFull ? 1u : 0u;
            protectedStacks += !decision.sell && !decision.discardWhenFull ? 1u : 0u;
            items << "\n   - Bag " << static_cast<uint32_t>(bag)
                  << " Slot " << static_cast<uint32_t>(slot) << ": "
                  << (proto ? proto->Name1 : "<unknown>")
                  << " x" << (item ? item->GetCount() : 0)
                  << " (Entry " << (proto ? proto->ItemId : 0)
                  << ", Quality " << (proto ? static_cast<uint32_t>(proto->Quality) : 0)
                  << ", Sell " << (proto ? proto->SellPrice : 0) << "c) -> "
                  << decision.reason;
            return true;
        });

        uint32_t freeSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);
        uint32_t projectedFreeSlots = freeSlots + sellableStacks + discardableStacks;
        uint32_t requiredSlots = plan.targetFreeBagSlots;
        if (requiredSlots == 0 && (bb.inv.bagsFull || Actions::LootAction::HasInventoryBlockedLoot(bot)))
            requiredSlots = 1;

        Creature* liveVendor = Helper::NpcUtils::FindNearbyServiceNpc(bot, true, false, 30.0f);
        uint32_t cachedVendorEntry = bb.inv.nearestVendorEntry;
        uint32_t vendorSuppression = cachedVendorEntry != 0
            ? brain->GetNpcSuppressionRemainingSeconds(cachedVendorEntry) : 0;
        std::string cachedVendor = cachedVendorEntry != 0
            ? fmt::format("Entry {} at ({:.1f}, {:.1f}, {:.1f}) Map {} | Distance {:.1f}yd | Suppressed {}s",
                cachedVendorEntry, bb.inv.vendorPosition.x, bb.inv.vendorPosition.y,
                bb.inv.vendorPosition.z, bb.inv.vendorPosition.mapId,
                Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                    bb.inv.vendorPosition.x, bb.inv.vendorPosition.y), vendorSuppression)
            : "None";

        std::ostringstream output;
        output << "[WorldBots Vendor Status] " << bot->GetName() << '\n'
               << " - Goal / Action: " << brain->GetGoalString() << " / " << brain->GetActionString() << '\n'
               << " - Progression: Level " << static_cast<uint32_t>(bot->GetLevel())
               << " | Quest ceiling +" << Config::BotConfig::GetQuestMaxLevelsAboveBot()
               << " | Grind range " << Config::BotConfig::GetGrindMinLevelOffset()
               << " to " << Config::BotConfig::GetGrindMaxLevelOffset()
               << " | Retry quests at level "
               << (brain->GetGrindUntilLevel() ? std::to_string(brain->GetGrindUntilLevel()) : "immediately") << '\n'
               << " - Bag Slots: " << freeSlots << '/' << bb.inv.totalBagSlots
               << " free | Low: " << (bb.inv.lowBagSpace ? "Yes" : "No")
               << " | Full: " << (bb.inv.bagsFull ? "Yes" : "No") << '\n'
               << " - Policy Stacks: Sell " << sellableStacks
               << " | Discard-if-full " << discardableStacks
               << " | Protected " << protectedStacks << '\n'
               << " - Cleanup Projection: " << freeSlots << " -> " << projectedFreeSlots
               << " free | Target " << requiredSlots
               << " | Can meet target: " << (projectedFreeSlots >= requiredSlots ? "Yes" : "No") << '\n'
               << " - Blocked Loot Corpses: " << Actions::LootAction::GetInventoryBlockedLootCount(bot) << '\n'
               << " - Cleanup Backoff: " << brain->GetInventoryCleanupRetryRemainingSeconds()
               << "s remaining | Free slots when blocked: " << brain->GetInventoryCleanupBlockedFreeSlots() << '\n'
               << " - Live Vendor Within 30yd: "
               << (liveVendor ? fmt::format("{} (Entry {})", liveVendor->GetName(), liveVendor->GetEntry()) : "None") << '\n'
               << " - Cached Vendor: " << cachedVendor << '\n'
               << " - Town Plan: Target slots " << plan.targetFreeBagSlots
               << " | Steps " << plan.steps.size()
               << " | Missing vendor " << (plan.blockedByMissingVendor ? "Yes" : "No")
               << " | Protected block " << (plan.blockedByProtectedInventory ? "Yes" : "No") << '\n'
               << " - Inventory Decisions:" << items.str();

        return EmitBotStatus(output.str());
    }

    std::string CoreLogic::GetBotQuestStatus(std::string const& botName)
    {
        std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
        auto runtime = guid ? s_botRuntimes.find(guid) : s_botRuntimes.end();
        if (!guid || runtime == s_botRuntimes.end() || !runtime->second.brain)
            return EmitBotStatus("Bot '" + botName + "' has no active brain or is not in world.");

        Brain::BotBrain* brain = runtime->second.brain.get();
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld())
            return EmitBotStatus("Bot '" + botName + "' is not currently in world.");

        const auto& bb = brain->GetBlackboard();
        std::ostringstream output;
        output << "[WorldBots Quest Status] " << bot->GetName() << '\n'
               << " - Goal / Action: " << brain->GetGoalString() << " / " << brain->GetActionString() << '\n'
               << " - Selected Quest: " << (brain->GetActiveQuestId() ? std::to_string(brain->GetActiveQuestId()) : "None") << '\n'
               << " - Progress Watch: Quest " << brain->GetQuestProgressWatchId()
               << " | Active-work elapsed " << brain->GetQuestProgressWatchElapsedMs() << "ms / 180000ms\n"
               << " - Inventory: " << bb.inv.freeBagSlots << '/' << bb.inv.totalBagSlots
               << " free | Blocked loot corpses " << Actions::LootAction::GetInventoryBlockedLootCount(bot)
               << " | Cleanup backoff " << brain->GetInventoryCleanupRetryRemainingSeconds() << "s\n"
               << " - Active Quests: " << bb.quest.activeQuests.size();

        for (const auto& quest : bb.quest.activeQuests)
        {
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(quest.questId);
            uint32_t suppressed = brain->GetQuestSuppressionRemainingSeconds(quest.questId);
            output << "\n   Quest " << quest.questId << " ('"
                   << (questTemplate ? questTemplate->GetTitle() : "Unknown") << "')"
                   << " | Level " << (questTemplate ? questTemplate->GetQuestLevel() : 0)
                   << " | " << (quest.questId == brain->GetActiveQuestId() ? "SELECTED" : "not selected")
                   << " | Suppressed " << suppressed << "s";
            if (quest.hasTargetPosition)
            {
                output << "\n     Target: Map " << quest.targetPosition.mapId << " at ("
                       << fmt::format("{:.1f}, {:.1f}, {:.1f}", quest.targetPosition.x,
                           quest.targetPosition.y, quest.targetPosition.z)
                       << ") | Distance " << fmt::format("{:.1f}", Helper::Distance2D(
                           bot->GetPositionX(), bot->GetPositionY(),
                           quest.targetPosition.x, quest.targetPosition.y)) << "yd";
            }
            else
                output << "\n     Target: unresolved";

            if (quest.objectives.empty())
                output << "\n     Objectives: none resolved";

            for (const auto& objective : quest.objectives)
            {
                output << "\n     - " << QuestObjectiveTypeName(objective.type)
                       << " Entry " << objective.targetEntry;
                if (objective.itemId != 0)
                {
                    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(objective.itemId);
                    output << " | Item " << objective.itemId << " ('"
                           << (itemTemplate ? itemTemplate->Name1 : "Unknown") << "')";
                }
                output << " | Progress " << objective.currentCount << '/' << objective.requiredCount;

                if (objective.type == Blackboard::QuestObjectiveType::CollectItem &&
                    objective.itemId != 0 && objective.currentCount < objective.requiredCount)
                {
                    ItemPosCountVec destination;
                    InventoryResult storeResult = bot->CanStoreNewItem(
                        NULL_BAG, NULL_SLOT, destination, objective.itemId, 1);
                    output << " | Store preflight: " << InventoryResultName(storeResult)
                           << " (" << static_cast<uint32_t>(storeResult) << ')';
                }
            }
        }

        output << "\n - Completed Quests: " << bb.quest.completedQuests.size();
        for (const auto& quest : bb.quest.completedQuests)
        {
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(quest.questId);
            output << "\n   - " << quest.questId << " ('"
                   << (questTemplate ? questTemplate->GetTitle() : "Unknown") << "')"
                   << " | Turn-in position " << (quest.hasTurnInPosition ? "resolved" : "missing")
                   << " | Reward inventory blocked "
                   << (Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate) ? "Yes" : "No");
        }

        auto suppressedQuests = brain->GetSuppressedQuests();
        output << "\n - Suspended Quests: " << suppressedQuests.size();
        for (const auto& [questId, remaining] : suppressedQuests)
            output << "\n   - Quest " << questId << ": " << remaining << "s remaining";

        return EmitBotStatus(output.str());
    }

    std::string CoreLogic::RunTestCommand(std::string const& arguments)
    {
        if (!Config::BotConfig::AreTestsEnabled())
            return "WorldBots tests are disabled. Set WorldBots.Tests.Enable = 1 to use .bot test commands.";

        std::string command = arguments;
        while (!command.empty() && command.front() == ' ')
            command.erase(command.begin());
        while (!command.empty() && command.back() == ' ')
            command.pop_back();

        if (command.empty() || command == "list")
            return Testing::ScenarioRunner::ListScenarios();
        if (command == "logic")
            return Testing::ScenarioRunner::RunLogicScenarios();

        constexpr std::string_view planPrefix = "plan";
        if (command.size() >= planPrefix.size() && command.compare(0, planPrefix.size(), planPrefix) == 0 &&
            (command.size() == planPrefix.size() || command[planPrefix.size()] == ' '))
        {
            std::string botName = command.size() == planPrefix.size()
                ? "Botharry" : command.substr(planPrefix.size() + 1);
            while (!botName.empty() && botName.front() == ' ')
                botName.erase(botName.begin());

            std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
            auto runtime = guid ? s_botRuntimes.find(guid) : s_botRuntimes.end();
            if (!guid || runtime == s_botRuntimes.end() || !runtime->second.brain)
                return "Bot '" + botName + "' has no active brain or is not in world.";

            return Testing::ScenarioRunner::DescribeTownPlan(
                formattedName, runtime->second.brain->PreviewTownPlan());
        }

        return Testing::ScenarioRunner::ListScenarios();
    }

    std::string CoreLogic::RunTraceCommand(std::string const& arguments)
    {
        if (!Config::BotConfig::AreTestsEnabled())
            return "WorldBots trace commands are disabled. Set WorldBots.Tests.Enable = 1 to use .bot trace.";

        std::string command = arguments;
        while (!command.empty() && command.front() == ' ')
            command.erase(command.begin());
        while (!command.empty() && command.back() == ' ')
            command.pop_back();

        std::size_t separator = command.find(' ');
        std::string botName = separator == std::string::npos ? command : command.substr(0, separator);
        std::string mode = separator == std::string::npos ? "status" : command.substr(separator + 1);
        while (!mode.empty() && mode.front() == ' ')
            mode.erase(mode.begin());

        if (botName.empty())
            return "Usage: .bot trace <name> on|off|status";
        if (mode != "on" && mode != "off" && mode != "status")
            return "Usage: .bot trace <name> on|off|status";

        std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(formattedName);
        auto runtime = guid ? s_botRuntimes.find(guid) : s_botRuntimes.end();
        if (!guid || runtime == s_botRuntimes.end() || !runtime->second.brain ||
            !runtime->second.brain->GetBot())
        {
            return "Bot '" + botName + "' has no active brain or is not in world.";
        }

        uint32_t guidLow = static_cast<uint32_t>(guid.GetCounter());
        if (mode == "on")
            Diagnostics::BotTrace::SetEnabled(guidLow, true);
        else if (mode == "off")
            Diagnostics::BotTrace::SetEnabled(guidLow, false);

        bool enabled = Diagnostics::BotTrace::IsEnabled(guidLow);
        std::string response = "Trace for bot '" + formattedName + "' is " +
            (enabled ? "enabled." : "disabled.");
        if (Diagnostics::BotTrace::IsGlobalVerbose())
            response += " WorldBots.VerboseLogging is enabled, so other bots still emit verbose logs.";
        return response;
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
            " - Login timeout/retries: {} ms / {}\n"
            " - Save batch: {} every {} ms (per-bot target {} ms)\n"
            " - Account mode: {}\n"
            " - Factory operations per tick: {}\n"
            " - Maximum bot count: {}",
            GetActiveBotCount(),
            Factory::BotFactory::GetPendingProvisionCount(),
            Factory::BotFactory::GetPendingSpawnCount(),
            BotAuth::GetPendingLoginCount(),
            Config::BotConfig::GetMaxConcurrentLogins(),
            Config::BotConfig::GetLoginTimeoutMs(),
            Config::BotConfig::GetLoginMaxRetries(),
            Config::BotConfig::GetSaveBatchSize(),
            Config::BotConfig::GetSaveBatchIntervalMs(),
            Config::BotConfig::GetSaveBotIntervalMs(),
            Config::BotConfig::UseDedicatedAccounts() ? "Dedicated (one marked account per bot)" : "Shared legacy account",
            Config::BotConfig::GetFactoryOperationsPerTick(),
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
            if (Unit* target = ObjectAccessor::FindPlayer(targetGuid))
            {
                mgr->FollowExternal(target, distance, angle);
            }
        }
    }

    void CoreLogic::BotChase(uint32_t botGuidLow, uint32_t targetGuidLow)
    {
        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            ObjectGuid targetGuid = ObjectGuid::Create<HighGuid::Player>(targetGuidLow);
            if (Unit* target = ObjectAccessor::FindPlayer(targetGuid))
            {
                mgr->ChaseExternal(target);
            }
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
        if (MovementManager* mgr = GetBotMovementManager(botGuidLow))
        {
            return static_cast<uint8_t>(mgr->GetState());
        }
        return static_cast<uint8_t>(BotMovementState::Idle);
    }
}
