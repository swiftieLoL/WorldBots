#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "BotBrain.h"
#include "Actions/IdleAction.h"
#include "Actions/MoveToAction.h"
#include "Actions/WanderAction.h"
#include "Actions/CombatAction.h"
#include "Actions/LootAction.h"
#include "Actions/VendorAction.h"
#include "Actions/RestAction.h"
#include "Actions/ResurrectAction.h"
#include "Actions/QuestAction.h"
#include "Actions/UnstuckAction.h"
#include "Actions/ActionFactory.h"
#include "Cache/BotCache.h"
#include "Blackboard/BlackboardUpdater.h"
#include "Helper/NpcFinder.h"
#include "Helper/QuestUtils.h"
#include "Helper/EvasionUtils.h"
#include "Helper/SpellLearningUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/ProgressionPolicy.h"
#include "Config/BotConfig.h"
#include "Town/TownPlanning.h"
#include "Diagnostics/BotTrace.h"
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
            default: return "Unknown";
        }
    }

    static bool FindVendorForInventory(Player* bot, uint32& vendorEntry,
        bool requireVendor, bool requireRepair, Cache::PositionInfo* resolvedPosition = nullptr)
    {
        vendorEntry = 0;
        if (!bot || !bot->IsInWorld())
            return false;

        if (Creature* liveService = Helper::NpcUtils::FindNearbyServiceNpc(
            bot, requireVendor, requireRepair, 30.0f))
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
            requireVendor, requireRepair, vendorEntry, vendorPosition);
        if (found && resolvedPosition)
            *resolvedPosition = vendorPosition;
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
            _activeAction->Start(bot, _movement);
        }
    }

    BotBrain::~BotBrain()
    {
        Shutdown();
    }

    void BotBrain::Shutdown()
    {
        Player* liveBot = ResolveBot();
        if (_activeAction && liveBot && _movement)
            _activeAction->Stop(liveBot, _movement);

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

    uint32_t BotBrain::GetInventoryCleanupRetryRemainingSeconds() const
    {
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        return _inventoryCleanupRetryAfterSec > nowSec ? _inventoryCleanupRetryAfterSec - nowSec : 0;
    }

    uint32_t BotBrain::GetQuestSuppressionRemainingSeconds(uint32_t questId) const
    {
        auto it = _blacklistedQuests.find(questId);
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        return it != _blacklistedQuests.end() && it->second > nowSec ? it->second - nowSec : 0;
    }

    uint32_t BotBrain::GetNpcSuppressionRemainingSeconds(uint32_t npcEntry) const
    {
        auto it = _blacklistedNpcs.find(npcEntry);
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        return it != _blacklistedNpcs.end() && it->second > nowSec ? it->second - nowSec : 0;
    }

    std::vector<std::pair<uint32_t, uint32_t>> BotBrain::GetSuppressedQuests() const
    {
        std::vector<std::pair<uint32_t, uint32_t>> result;
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        for (const auto& [questId, expirySec] : _blacklistedQuests)
        {
            if (expirySec > nowSec)
                result.emplace_back(questId, expirySec - nowSec);
        }
        std::sort(result.begin(), result.end());
        return result;
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

    void BotBrain::Sense(uint32_t deltaMs)
    {
        Player* bot = ResolveBot();
        if (!bot) return;
        Blackboard::BlackboardUpdater::UpdateAll(bot, _movement, _blackboard, deltaMs);
        Helper::SpellLearningUtils::AutoLearnClassSpells(bot, _lastLearnedLevel);
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
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        auto pruneExpired = [nowSec](auto& entries) {
            for (auto it = entries.begin(); it != entries.end(); )
                it = nowSec >= it->second ? entries.erase(it) : std::next(it);
        };
        pruneExpired(_blacklistedQuests);
        pruneExpired(_blacklistedNpcs);

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

        // Priority 0: Ghost & Graveyard Resurrect State
        if (!bot->IsAlive() || _blackboard.self.isDead)
        {
            if (_goal != BotGoal::Resurrect)
            {
                _deathTimestamps.push_back(nowSec);

                // Prune death timestamps older than 120 seconds (2 minutes)
                _deathTimestamps.erase(
                    std::remove_if(_deathTimestamps.begin(), _deathTimestamps.end(),
                        [nowSec](uint32_t t) { return (nowSec - t) > 120; }),
                    _deathTimestamps.end());

                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' died! (Deaths in last 2 minutes: {})",
                        bot->GetName(), _deathTimestamps.size());
                }

                if (_deathTimestamps.size() >= 5)
                {
                    _deadlyQuestId = 0;
                    if (_goal != BotGoal::Grind && !_blackboard.quest.activeQuests.empty())
                    {
                        _deadlyQuestId = _activeQuestId != 0 ? _activeQuestId : _blackboard.quest.activeQuests.front().questId;
                        uint32_t expirySec = static_cast<uint32_t>(time(nullptr)) + 900; // 15-minute blacklist
                        _blacklistedQuests[_deadlyQuestId] = expirySec;
                    }
                    if (Config::BotConfig::IsGrindFallbackEnabled())
                        _grindUntilLevel = Helper::NextProgressionRetryLevel(bot->GetLevel());

                    if (Diagnostics::BotTrace::ShouldLog(bot))
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' died 5+ times in the last 2 minutes on Quest {}! Triggering UnstuckAction!",
                            bot->GetName(), _deadlyQuestId);
                    }

                    _deathTimestamps.clear();
                    _teleportTimerMs = 2500;
                    SetGoal(BotGoal::Unstuck);
                    return;
                }
            }

            SetGoal(BotGoal::Resurrect);
            return;
        }

        // Emergency flee before combat when health is critically low.
        if (_blackboard.self.inCombat && _blackboard.self.healthPct < _tuning.fleeHealthPct &&
            (!_blackboard.combat.attackerGuids.empty() || _blackboard.combat.currentTargetGuid))
        {
            SetGoal(BotGoal::Flee);
            return;
        }

        // Priority 1: Combat (If bot is in combat or has attackers or target)
        if (_blackboard.self.inCombat || !_blackboard.combat.attackerGuids.empty() || _blackboard.combat.currentTargetGuid)
        {
            // Don't interrupt ProgressQuestAction's self-managed combat.
            // ProgressQuestAction handles target engagement (attack + movement) internally,
            // so switching to CombatAction would destroy quest state and cause oscillation.
            bool isProgressQuestCombat = (_goal == BotGoal::ProgressQuest && _activeAction && !_activeAction->IsComplete());
            bool isGrindCombat = (_goal == BotGoal::Grind && _activeAction && !_activeAction->IsComplete());
            bool hasActiveEngagement = (!_blackboard.combat.attackerGuids.empty() || _blackboard.combat.currentTargetGuid);

            if ((isProgressQuestCombat || isGrindCombat) && hasActiveEngagement)
            {
                return; // ProgressQuestAction and GrindAction handle their own combat.
            }

            if (!isProgressQuestCombat && !isGrindCombat)
            {
                SetGoal(BotGoal::Combat);
                return;
            }

            // ProgressQuest with only inCombat cooldown timer - fall through to lower priorities
        }

        // Priority 1.5: Out-of-Combat Vital Recovery (Rest & Consumables)
        bool isLowVitals = _blackboard.self.healthPct < _tuning.restHealthPct ||
            (_blackboard.self.manaPct < _tuning.restManaPct && bot->GetMaxPower(POWER_MANA) > 0);
        bool isRecovering = (_goal == BotGoal::Rest) && (_blackboard.self.healthPct < 85 || (_blackboard.self.manaPct < 85 && bot->GetMaxPower(POWER_MANA) > 0));

        if ((isLowVitals || isRecovering) && !_blackboard.self.inCombat && _blackboard.combat.attackerGuids.empty())
        {
            SetGoal(BotGoal::Rest);
            return;
        }

        // LootAction can discover a full inventory before the periodic
        // inventory scan identifies sellable junk. Route directly to a
        // vendor so the same loot target is not retried in a tight loop.
        bool hasInventoryBlockedLoot = Actions::LootAction::HasInventoryBlockedLoot(bot);
        bool hasQuestItemCapacityBlock = false;
        for (const auto& activeQuest : _blackboard.quest.activeQuests)
        {
            if (_blacklistedQuests.find(activeQuest.questId) == _blacklistedQuests.end() &&
                HasInventoryBlockedItemObjective(bot, activeQuest))
            {
                hasQuestItemCapacityBlock = true;
                break;
            }
        }

        // Do not kill a quest-item source before the required item can fit.
        // Create one slot proactively, rather than waiting for a unique corpse
        // to expose the capacity problem after combat.
        if (hasQuestItemCapacityBlock && !inventoryCleanupDeferred)
        {
            uint32 vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, _blackboard.inv.needsRepair) &&
                _blacklistedNpcs.find(vendorEntry) == _blacklistedNpcs.end())
            {
                SetGoal(BotGoal::TownRun);
                return;
            }
        }

        if (hasInventoryBlockedLoot && !inventoryCleanupDeferred)
        {
            uint32 vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, _blackboard.inv.needsRepair) &&
                _blacklistedNpcs.find(vendorEntry) == _blacklistedNpcs.end())
            {
                SetGoal(BotGoal::TownRun);
                return;
            }
        }

        // A completed quest whose reward cannot fit belongs to VendorAction,
        // not TurnInQuestAction. Do this before selecting a turn-in task so
        // the bot clears capacity first and only approaches the quest giver
        // when RewardQuest can succeed.
        for (const auto& completed : _blackboard.quest.completedQuests)
        {
            if (inventoryCleanupDeferred)
                break;
            if (_blacklistedQuests.find(completed.questId) != _blacklistedQuests.end())
                continue;
            if (completed.hasTurnInPosition && completed.turnInPosition.mapId != bot->GetMapId())
                continue;

            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
            if (!Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate))
                continue;

            uint32 vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, true, _blackboard.inv.needsRepair) &&
                _blacklistedNpcs.find(vendorEntry) == _blacklistedNpcs.end())
            {
                if (_goal != BotGoal::TownRun && Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' cannot receive the reward for completed quest {} ('{}') with its current inventory; selecting VendorAction before TurnInQuestAction",
                        bot->GetName(), completed.questId, questTemplate->GetTitle());
                }
                SetGoal(BotGoal::TownRun);
                return;
            }
        }

        // Priority 1.8: Emergency Vendoring if bags are 100% full and bot has items to sell or gear to repair
        if (!inventoryCleanupDeferred && _blackboard.inv.bagsFull &&
            (_blackboard.inv.hasItemsToSell || _blackboard.inv.needsRepair))
        {
            uint32_t vendorEntry = 0;
            if (FindVendorForInventory(bot, vendorEntry, _blackboard.inv.hasItemsToSell, _blackboard.inv.needsRepair) &&
                _blacklistedNpcs.find(vendorEntry) == _blacklistedNpcs.end())
            {
                SetGoal(BotGoal::TownRun);
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
        if (!inventoryCleanupDeferred &&
            ((_blackboard.inv.lowBagSpace && _blackboard.inv.hasItemsToSell) || _blackboard.inv.needsRepair))
        {
            uint32_t vendorEntry = 0;
            bool requireVendor = _blackboard.inv.lowBagSpace && _blackboard.inv.hasItemsToSell;
            if (FindVendorForInventory(bot, vendorEntry, requireVendor, _blackboard.inv.needsRepair) &&
                _blacklistedNpcs.find(vendorEntry) == _blacklistedNpcs.end())
            {
                SetGoal(BotGoal::TownRun);
                return;
            }
        }

        // Priority 3.5: Turn in completed quests
        if (!_blackboard.quest.completedQuests.empty())
        {
            for (const auto& completed : _blackboard.quest.completedQuests)
            {
                if (_blacklistedQuests.find(completed.questId) != _blacklistedQuests.end())
                    continue;
                if (completed.hasTurnInPosition && completed.turnInPosition.mapId != bot->GetMapId())
                    continue;
                if (inventoryCleanupDeferred)
                {
                    Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
                    if (Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate))
                        continue;
                }

                if (completed.hasTurnInPosition)
                {
                    _activeQuestId = completed.questId;
                    SetGoal(BotGoal::TownRun);
                    return;
                }
            }
        }

        // Priority 5: Progress active in-progress quests
        if (_grindUntilLevel != 0 && bot->GetLevel() >= _grindUntilLevel)
        {
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' reached retry level {}; quest progression is eligible again",
                    bot->GetName(), bot->GetLevel());
            _grindUntilLevel = 0;
        }
        if (Config::BotConfig::IsGrindFallbackEnabled() &&
            _grindUntilLevel != 0 && bot->GetLevel() < _grindUntilLevel)
        {
            _activeQuestId = 0;
            SetGoal(BotGoal::Grind);
            return;
        }

        auto isQuestSuitable = [bot](uint32_t questId) {
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
            return !questTemplate || Helper::IsQuestLevelSuitable(bot->GetLevel(),
                questTemplate->GetQuestLevel(), Config::BotConfig::GetQuestMaxLevelsAboveBot());
        };

        bool hasValidActiveQuest = false;
        bool hasDeferredProgressionWork = false;
        uint32_t selectedQuestId = 0;
        float selectedDistanceSq = std::numeric_limits<float>::max();
        for (const auto& q : _blackboard.quest.activeQuests)
        {
            if (_blacklistedQuests.find(q.questId) != _blacklistedQuests.end() ||
                !isQuestSuitable(q.questId) ||
                (q.hasTargetPosition && q.targetPosition.mapId != bot->GetMapId()) ||
                HasInventoryBlockedItemObjective(bot, q))
            {
                hasDeferredProgressionWork = true;
                continue;
            }
            if (_blacklistedQuests.find(q.questId) == _blacklistedQuests.end() &&
                (!q.hasTargetPosition || q.targetPosition.mapId == bot->GetMapId()) &&
                !HasInventoryBlockedItemObjective(bot, q))
            {
                float distanceSq = q.hasTargetPosition
                    ? Helper::DistanceSq(q.targetPosition.x, q.targetPosition.y, q.targetPosition.z,
                        _blackboard.self.x, _blackboard.self.y, _blackboard.self.z)
                    : std::numeric_limits<float>::max() - 1.0f;
                if (q.questId == _activeQuestId)
                    distanceSq = -1.0f; // Preserve quest context across loot/rest/vendor interruptions.
                if (!hasValidActiveQuest || distanceSq < selectedDistanceSq)
                {
                    hasValidActiveQuest = true;
                    selectedQuestId = q.questId;
                    selectedDistanceSq = distanceSq;
                }
            }
        }

        if (hasValidActiveQuest)
        {
            _activeQuestId = selectedQuestId;
            SetGoal(BotGoal::ProgressQuest);
            return;
        }
        _activeQuestId = 0;

        // Priority 6: Accept another quest when no active quest is currently
        // actionable. Suspended or cross-map quests must not monopolize the log.
        if (!hasValidActiveQuest && !_blackboard.quest.availableQuests.empty())
        {
            for (const auto& available : _blackboard.quest.availableQuests)
            {
                if (_blacklistedQuests.find(available.questId) != _blacklistedQuests.end() ||
                    !isQuestSuitable(available.questId))
                {
                    hasDeferredProgressionWork = true;
                    continue;
                }

                if (available.hasQuestGiverPosition &&
                    available.questGiverPosition.mapId == bot->GetMapId())
                {
                    _activeQuestId = available.questId;
                    SetGoal(BotGoal::AcceptQuest);
                    return;
                }
            }
        }

        // An unsuitable or repeatedly failing quest is progression work, but
        // not work the bot should hammer immediately. Hunt conservative mobs
        // on this map; the action can travel to another same-level area.
        if (Config::BotConfig::IsGrindFallbackEnabled() &&
            (hasDeferredProgressionWork || !_blackboard.quest.activeQuests.empty() ||
             !_blackboard.quest.availableQuests.empty()))
        {
            SetGoal(BotGoal::Grind);
            return;
        }

        // Follow a group leader only when there is no quest work to perform.
        if (_blackboard.party.isInGroup && !_blackboard.party.isGroupLeader && _blackboard.party.groupLeaderGuid)
        {
            SetGoal(BotGoal::FollowTarget);
            return;
        }

        // With no known quest work at all, grinding is also a productive way
        // to discover a suitable area. Wander remains the opt-out behavior.
        SetGoal(Config::BotConfig::IsGrindFallbackEnabled() ? BotGoal::Grind : BotGoal::Wander);
    }

    Town::Plan BotBrain::PreviewTownPlan() const
    {
        Player* bot = ResolveBot();
        if (!bot)
            return {};

        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        bool inventoryCleanupDeferred = _inventoryCleanupRetryAfterSec != 0 &&
            nowSec < _inventoryCleanupRetryAfterSec &&
            _blackboard.inv.freeBagSlots <= _inventoryCleanupBlockedFreeSlots;

        Town::PlanningInput input;
        input.freeBagSlots = _blackboard.inv.freeBagSlots;
        input.hasSellableItems = _blackboard.inv.hasItemsToSell;
        input.needsInventoryCleanup = !inventoryCleanupDeferred &&
            (_blackboard.inv.lowBagSpace || _blackboard.inv.bagsFull ||
             Actions::LootAction::HasInventoryBlockedLoot(bot));
        input.needsRepair = _blackboard.inv.needsRepair;

        bool hasBlockedReward = false;
        for (const auto& completed : _blackboard.quest.completedQuests)
        {
            if (_blacklistedQuests.find(completed.questId) != _blacklistedQuests.end())
                continue;

            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
            Town::QuestTurnInCandidate candidate;
            candidate.questId = completed.questId;
            candidate.rewardBlocked = questTemplate &&
                Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate);
            if (inventoryCleanupDeferred && candidate.rewardBlocked)
                continue;
            candidate.onCurrentMap = !completed.hasTurnInPosition ||
                completed.turnInPosition.mapId == bot->GetMapId();
            candidate.hasKnownPosition = completed.hasTurnInPosition;
            hasBlockedReward = hasBlockedReward ||
                (candidate.rewardBlocked && candidate.onCurrentMap && candidate.hasKnownPosition);
            input.completedQuests.push_back(candidate);
        }

        bool needsRewardSpace = hasBlockedReward && input.freeBagSlots < input.rewardReserveSlots;
        bool requireVendor = input.needsInventoryCleanup || needsRewardSpace;
        bool needsServiceNpc = requireVendor || input.needsRepair;
        Cache::PositionInfo servicePosition;
        uint32_t vendorEntry = 0;
        input.hasUsableVendor = needsServiceNpc &&
            FindVendorForInventory(bot, vendorEntry, requireVendor, input.needsRepair, &servicePosition) &&
            _blacklistedNpcs.find(vendorEntry) == _blacklistedNpcs.end();

        float originX = input.hasUsableVendor ? servicePosition.x : bot->GetPositionX();
        float originY = input.hasUsableVendor ? servicePosition.y : bot->GetPositionY();
        float originZ = input.hasUsableVendor ? servicePosition.z : bot->GetPositionZ();
        for (Town::QuestTurnInCandidate& candidate : input.completedQuests)
        {
            auto completed = std::find_if(_blackboard.quest.completedQuests.begin(),
                _blackboard.quest.completedQuests.end(), [&candidate](const auto& quest) {
                    return quest.questId == candidate.questId;
                });
            if (completed != _blackboard.quest.completedQuests.end() && completed->hasTurnInPosition)
            {
                candidate.distanceFromTownSq = Helper::DistanceSq(
                    completed->turnInPosition.x, completed->turnInPosition.y, completed->turnInPosition.z,
                    originX, originY, originZ);
            }
        }

        return Town::BuildPlan(input);
    }

    ActionRequest BotBrain::BuildActionRequest() const
    {
        auto wanderRequest = [&]() -> ActionRequest {
            WanderActionRequest wander;
            wander.origin = { _blackboard.self.x, _blackboard.self.y, _blackboard.self.z, _blackboard.self.mapId };
            wander.radius = 15.0f;
            wander.suppressedQuests = _blacklistedQuests;
            return { BotGoal::Wander, std::move(wander) };
        };

        switch (_goal)
        {
            case BotGoal::Combat:
            {
                ObjectGuid targetGuid = _blackboard.combat.currentTargetGuid;
                if (!targetGuid) targetGuid = _blackboard.combat.primaryAttackerGuid;
                if (!targetGuid) targetGuid = _blackboard.spatial.nearestEnemyGuid;
                return targetGuid ? ActionRequest{ _goal, TargetActionRequest{ targetGuid } } : wanderRequest();
            }

            case BotGoal::FollowTarget:
            {
                ObjectGuid targetGuid = _blackboard.party.groupLeaderGuid;
                if (!targetGuid) targetGuid = _blackboard.spatial.nearestFriendlyGuid;
                return targetGuid ? ActionRequest{ _goal, TargetActionRequest{ targetGuid } }
                                  : ActionRequest{ BotGoal::Idle, std::monostate{} };
            }

            case BotGoal::Flee:
            {
                ObjectGuid targetGuid = _blackboard.combat.primaryAttackerGuid;
                if (!targetGuid) targetGuid = _blackboard.combat.currentTargetGuid;
                if (!targetGuid) targetGuid = _blackboard.spatial.nearestEnemyGuid;
                return targetGuid ? ActionRequest{ _goal, TargetActionRequest{ targetGuid } }
                                  : ActionRequest{ BotGoal::Idle, std::monostate{} };
            }

            case BotGoal::AcceptQuest:
            case BotGoal::TurnInQuest:
            case BotGoal::ProgressQuest:
                return { _goal, QuestActionRequest{ _activeQuestId } };

            case BotGoal::MoveToNpc:
                if (!_blackboard.quest.availableQuests.empty())
                    return { _goal, MoveActionRequest{ _blackboard.quest.availableQuests.front().questGiverPosition } };
                if (!_blackboard.quest.completedQuests.empty())
                    return { _goal, MoveActionRequest{ _blackboard.quest.completedQuests.front().turnInPosition } };
                return { BotGoal::Idle, std::monostate{} };

            case BotGoal::Unstuck:
                return { _goal, UnstuckActionRequest{ _deadlyQuestId } };

            case BotGoal::TownRun:
            {
                Town::Plan plan = PreviewTownPlan();
                return plan.Empty() && !plan.blockedByMissingVendor && !plan.blockedByProtectedInventory
                    ? ActionRequest{ BotGoal::Idle, std::monostate{} }
                    : ActionRequest{ _goal, TownRunActionRequest{ std::move(plan) } };
            }

            case BotGoal::Wander:
                return wanderRequest();

            case BotGoal::Grind:
                return { _goal, GrindActionRequest{
                    Config::BotConfig::GetGrindMinLevelOffset(),
                    Config::BotConfig::GetGrindMaxLevelOffset() } };

            case BotGoal::Loot:
            case BotGoal::Vendor:
            case BotGoal::Rest:
            case BotGoal::Resurrect:
            case BotGoal::Idle:
            default:
                return { _goal, std::monostate{} };
        }
    }

    void BotBrain::Think(uint32_t deltaMs)
    {
        Player* bot = ResolveBot();
        if (!bot) return;

        // Explicit script movement owns the bot until BotStop releases its
        // lease. Freeze goal/action timers as well as movement commands so an
        // external route cannot cause unrelated quest failures or suppression.
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

        // Consume a completed quest action's explicit outcome before selecting
        // the next goal. This prevents a blocked quest from being recreated on
        // every Think tick.
        if (_activeAction && _activeAction->IsComplete())
        {
            Actions::ActionOutcome outcome = _activeAction->GetOutcome();
            uint32_t questId = _activeAction->GetRelatedQuestId();
            uint32_t npcEntry = _activeAction->GetRelatedNpcEntry();
            if (_activeAction->IsInventoryCapacityFailure())
            {
                constexpr uint32_t InventoryCleanupBackoffSeconds = 900;
                _inventoryCleanupRetryAfterSec = static_cast<uint32_t>(time(nullptr)) + InventoryCleanupBackoffSeconds;
                _inventoryCleanupBlockedFreeSlots = _blackboard.inv.freeBagSlots;
                TC_LOG_WARN("server", "[WorldBots] [Inventory] Bot '{}' deferred inventory cleanup for {} seconds because no additional safe space could be created; protected items will not be destroyed",
                    bot->GetName(), InventoryCleanupBackoffSeconds);
            }
            if (questId != 0 && (outcome == Actions::ActionOutcome::Blocked ||
                                 outcome == Actions::ActionOutcome::Unsupported ||
                                 outcome == Actions::ActionOutcome::RetryableFailure))
            {
                uint32_t suppressSeconds = outcome == Actions::ActionOutcome::Unsupported ? 3600 :
                    (outcome == Actions::ActionOutcome::Blocked ? 900 : 60);
                _blacklistedQuests[questId] = static_cast<uint32_t>(time(nullptr)) + suppressSeconds;
                if (outcome == Actions::ActionOutcome::Blocked && Config::BotConfig::IsGrindFallbackEnabled())
                    _grindUntilLevel = Helper::NextProgressionRetryLevel(bot->GetLevel());
                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' suspended quest {} for {} seconds after action outcome {}: {}",
                    bot->GetName(), questId, suppressSeconds, static_cast<uint32_t>(outcome),
                    _activeAction->GetOutcomeReason().empty() ? "unspecified failure" : _activeAction->GetOutcomeReason());
                if (_activeQuestId == questId)
                    _activeQuestId = 0;
            }
            if (npcEntry != 0 && (outcome == Actions::ActionOutcome::Blocked ||
                                  outcome == Actions::ActionOutcome::Unsupported ||
                                  outcome == Actions::ActionOutcome::RetryableFailure))
            {
                uint32_t suppressSeconds = outcome == Actions::ActionOutcome::Unsupported ? 3600 :
                    (outcome == Actions::ActionOutcome::Blocked ? 900 : 60);
                _blacklistedNpcs[npcEntry] = static_cast<uint32_t>(time(nullptr)) + suppressSeconds;
                TC_LOG_WARN("server", "[WorldBots] Bot '{}' suspended NPC Entry {} for {} seconds after action outcome {}: {}",
                    bot->GetName(), npcEntry, suppressSeconds, static_cast<uint32_t>(outcome),
                    _activeAction->GetOutcomeReason().empty() ? "unspecified failure" : _activeAction->GetOutcomeReason());
            }
        }

        // Persistent logical-progress watchdog. It survives expected
        // interruptions, but only accumulates while this quest is actually
        // executing in its objective area. Town runs, loot, rest, and long
        // travel must not consume the quest's work budget.
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
            uint64_t signature = CalculateQuestProgressSignature(*watchedQuest);
            if (_progressWatchQuestId != watchedQuest->questId || _progressWatchSignature != signature)
            {
                _progressWatchQuestId = watchedQuest->questId;
                _progressWatchSignature = signature;
                _progressWatchNoChangeMs = 0;
            }
            else
            {
                bool activelyExecutingQuest = _goal == BotGoal::ProgressQuest &&
                    _activeAction && !_activeAction->IsComplete() &&
                    _activeAction->GetRelatedQuestId() == watchedQuest->questId;
                bool travellingToObjective = false;
                if (activelyExecutingQuest && watchedQuest->hasTargetPosition && _movement &&
                    _movement->GetState() != BotMovementState::Idle)
                {
                    travellingToObjective = Helper::DistanceSq(
                        _blackboard.self.x, _blackboard.self.y, _blackboard.self.z,
                        watchedQuest->targetPosition.x, watchedQuest->targetPosition.y,
                        watchedQuest->targetPosition.z) > 900.0f;
                }

                if (activelyExecutingQuest && !travellingToObjective)
                    _progressWatchNoChangeMs += deltaMs;

                if (_progressWatchNoChangeMs >= 180000)
                {
                    _blacklistedQuests[watchedQuest->questId] = static_cast<uint32_t>(time(nullptr)) + 900;
                    if (Config::BotConfig::IsGrindFallbackEnabled())
                        _grindUntilLevel = Helper::NextProgressionRetryLevel(bot->GetLevel());
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' suspended quest {} after 180 seconds of active objective work without counter progress",
                        bot->GetName(), watchedQuest->questId);
                    _activeQuestId = 0;
                    _progressWatchQuestId = 0;
                    _progressWatchSignature = 0;
                    _progressWatchNoChangeMs = 0;
                }
            }
        }
        else if (_progressWatchQuestId != 0)
        {
            _progressWatchQuestId = 0;
            _progressWatchSignature = 0;
            _progressWatchNoChangeMs = 0;
        }

        EvaluateGoals();

        // Update decoupled Global Stuck Detector
        if (_stuckDetector.Update(bot, _movement, _goal, _blackboard, _activeQuestId,
                                  _blacklistedQuests, _blacklistedNpcs, deltaMs))
        {
            _recoveryPauseMs = 1000;
        }

        // Manage Action transitions via ActionFactory
        if (!_activeAction || _activeAction->IsComplete())
        {
            ActionRequest request = BuildActionRequest();
            // BuildActionRequest may deliberately fall back when the desired
            // goal no longer has an executable payload. Keep the recorded
            // goal and concrete action synchronized so a non-completing
            // fallback such as IdleAction cannot permanently pin the old goal.
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
        if (_movement && _movement->IsExternallyControlled()) return;
        if (_recoveryPauseMs > 0)
        {
            _recoveryPauseMs = _recoveryPauseMs > deltaMs ? _recoveryPauseMs - deltaMs : 0;
            return;
        }
        if (_activeAction && _movement)
        {
            _activeAction->Update(bot, _movement, _blackboard, deltaMs);
        }
    }

    void BotBrain::SetGoal(BotGoal newGoal)
    {
        Player* bot = ResolveBot();
        if (_goal != newGoal)
        {
            // Non-interruptible active actions can only be interrupted by emergency goals
            if (_activeAction && !_activeAction->IsComplete() && !_activeAction->IsInterruptible())
            {
                if (newGoal != BotGoal::Combat && newGoal != BotGoal::Flee &&
                    newGoal != BotGoal::Resurrect && newGoal != BotGoal::Unstuck)
                {
                    return;
                }
            }

            if (bot && Diagnostics::BotTrace::ShouldLog(bot))
            {
                if (newGoal == BotGoal::Vendor || newGoal == BotGoal::TownRun)
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) Goal changed: {} -> {} (Reason: {}, FreeBagSlots: {}/{}, HasSellableItems: {}, NeedsRepair: {}) (AvailableQuests: {}, ActiveQuests: {}, CompletedQuests: {})",
                        bot->GetName(), bot->GetGUID().GetCounter(), BotGoalToString(_goal),
                        BotGoalToString(newGoal),
                        _blackboard.inv.bagsFull ? "Bags Completely Full" : (_blackboard.inv.hasItemsToSell ? "Has Gray Items To Sell" : "Equipment Needs Repair"),
                        _blackboard.inv.freeBagSlots, _blackboard.inv.totalBagSlots,
                        _blackboard.inv.hasItemsToSell ? "Yes" : "No",
                        _blackboard.inv.needsRepair ? "Yes" : "No",
                        _blackboard.quest.availableQuests.size(), _blackboard.quest.activeQuests.size(), _blackboard.quest.completedQuests.size());
                }
                else
                {
                    TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) Goal changed: {} -> {} (AvailableQuests: {}, ActiveQuests: {}, CompletedQuests: {})",
                        bot->GetName(), bot->GetGUID().GetCounter(), BotGoalToString(_goal), BotGoalToString(newGoal),
                        _blackboard.quest.availableQuests.size(), _blackboard.quest.activeQuests.size(), _blackboard.quest.completedQuests.size());
                }
            }

            _goal = newGoal;
            SetAction(nullptr);
        }
    }

    void BotBrain::SetAction(std::unique_ptr<Actions::BotAction> newAction)
    {
        Player* bot = ResolveBot();
        if (_activeAction && bot && _movement)
        {
            _activeAction->Stop(bot, _movement);
        }

        _activeAction = std::move(newAction);

        if (_activeAction && bot && _movement)
        {
            _activeAction->Start(bot, _movement);
        }
    }
}
