#include "QuestAction.h"
#include "QuestVendorPurchasePolicy.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "Map.h"
#include "WorldSession.h"
#include "Conditions/ConditionMgr.h"
#include "DataStores/DBCStores.h"
#include "Cache/BotCache.h"
#include "Helper/NpcFinder.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Helper/ProgressionPolicy.h"
#include "Helper/SpellUtils.h"
#include "Helper/TimeUtils.h"
#include "Config/BotConfig.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/StructuredEventLog.h"
#include "ObjectiveHandler.h"
#include "ExplorationObjectiveHandler.h"
#include "TalkDeliveryObjectiveHandler.h"
#include "CreditObjectiveHandler.h"
#include "Party/PartyCombat.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <sstream>
#include <unordered_set>

namespace Actions
{
    namespace
    {
        enum ProgressSubPath : uint8_t
        {
            SubPath_None = 0,
            SubPath_Travel = 10,
            SubPath_Interact = 11,
            SubPath_Combat = 20,
            SubPath_Wander = 30
        };

        constexpr uint32_t ObjectiveNoProgressTimeoutMs = 120000;
        constexpr uint32_t InteractionNoProgressTimeoutMs = 30000;
        constexpr float ObjectiveInteractionRange = 3.5f;
        constexpr uint32_t EmblazonedRunebladeQuestId = 12619;
        constexpr uint32_t BattleWornSwordItemId = 38607;
        constexpr uint32_t RunebladedSwordItemId = 38631;
        constexpr uint32_t BattleWornSwordGameObjectEntry = 190584;
        constexpr uint32_t RuneforgeCreatureEntry = 28481;

        struct ObjectiveGameObjectCheck
        {
            Player* bot;
            const std::unordered_set<uint32_t>& entries;
            float range;

            bool operator()(GameObject* go) const
            {
                return go && go->isSpawned() && go->GetGoState() == GO_STATE_READY &&
                    entries.find(go->GetEntry()) != entries.end() && bot->GetDistance(go) <= range;
            }
        };
    }

    ProgressQuestAction::ProgressQuestAction(uint32_t questId,
        std::vector<Brain::DangerArea> dangerAreas)
        : _lockedQuestId(questId), _lastProgressSubPath(SubPath_None),
          _searchExpandTimerMs(0), _searchExpandCount(0),
          _dangerAreas(std::move(dangerAreas))
    {
    }

    void ProgressQuestAction::Start(Player* bot, MovementManager* /*movement*/)
    {
        ResetOutcome();
        _objectiveTargetGuid.Clear();
        _classStrategy.reset();
        _lastProgressSubPath = SubPath_None;
        _searchExpandTimerMs = 0;
        _searchExpandCount = 0;
        _sourceRecoveryCooldownMs = 0;
        _noProgressTimerMs = 0;
        _targetAcquireTimerMs = 0;
        _interactionRetryTimerMs = 0;
        _creatureSearchCooldownMs = 0;
        _gameObjectSearchCooldownMs = 0;
        _lastProgressSignature = 0;
        _progressActivitySignature = 0;
        _lastCombatActivitySignature = 0;
        _suppressedObjectiveTargets.clear();
        _objectiveTargetPathFailures.Reset();
        _objectiveDestinationPathFailures.Reset();
        _objectiveTargetApproachProgress.Reset();
        _retryDelaySeconds = 0;
        _worldTravel.Reset();

        if (!_lockedQuestId)
            Finish(ActionOutcome::Blocked, "the brain did not provide a quest context",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
        else if (bot)
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
    }

    uint64_t ProgressQuestAction::CalculateProgressSignature(const Blackboard::ActiveQuest& active) const
    {
        uint64_t signature = 1469598103934665603ULL;
        auto mix = [&signature](uint64_t value) {
            signature ^= value;
            signature *= 1099511628211ULL;
        };

        mix(active.questId);
        mix(active.requiresExploration ? 1 : 0);
        for (const auto& objective : active.objectives)
        {
            mix(static_cast<uint8_t>(objective.type));
            mix(static_cast<uint8_t>(objective.targetKind));
            mix(objective.targetEntry);
            mix(objective.itemId);
            mix(objective.currentCount);
            mix(objective.requiredCount);
            mix(objective.combatLevelSuitable ? 1 : 0);
            mix(objective.vendorPurchase ? 1 : 0);
        }
        return signature;
    }

    bool ProgressQuestAction::HandleCombatTarget(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, Unit* target, uint32_t deltaMs)
    {
        if (!bot || !target || !target->IsInWorld() || !target->IsAlive() || target->GetMap() != bot->GetMap())
            return false;

        if (!_classStrategy)
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
        if (!_classStrategy)
            return false;

        // Quest counters generally advance only after a kill or loot. Expose
        // target-health changes so both no-progress watchdogs can distinguish
        // a long but productive fight from a genuinely stalled objective.
        uint64_t activitySignature = target->GetGUID().GetRawValue();
        activitySignature ^= static_cast<uint64_t>(target->GetHealth()) +
            0x9e3779b97f4a7c15ULL + (activitySignature << 6) + (activitySignature >> 2);
        _progressActivitySignature = activitySignature;

        _objectiveTargetGuid = target->GetGUID();
        bot->SetSelection(target->GetGUID());
        if (_lastProgressSubPath != SubPath_Combat)
        {
            _lastProgressSubPath = SubPath_Combat;
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' engaging '{}' (Entry: {}, GUID: {}) while progressing quest {}",
                    bot->GetName(), target->GetName(), target->GetEntry(), target->GetGUID().GetCounter(), _lockedQuestId);
        }
        std::ostringstream pathSource;
        pathSource << "quest_live_target;entry=" << target->GetEntry()
            << ";guid=" << target->GetGUID().GetCounter();
        movement->SetDiagnosticPathSource(pathSource.str());
        uint64_t pathGeneration = movement->GetPathAttemptGeneration();
        bool roleHandled = Party::HandleRoleAction(bot, target, movement,
            blackboard);
        if (!roleHandled)
            _classStrategy->UpdateCombat(bot, target, movement, blackboard, deltaMs);
        if (HandleRepeatedObjectiveTargetPathFailure(bot, movement, target,
            pathGeneration))
        {
            return true;
        }
        HandleObjectiveTargetApproachStall(bot, movement, target,
            pathGeneration, deltaMs);
        return true;
    }

    bool ProgressQuestAction::IsObjectiveTargetSuppressed(Unit* target) const
    {
        if (!target)
            return false;
        auto it = _suppressedObjectiveTargets.find(
            target->GetGUID().GetRawValue());
        return it != _suppressedObjectiveTargets.end() &&
            Helper::MonotonicSeconds() < it->second;
    }

    bool ProgressQuestAction::HandleRepeatedObjectiveTargetPathFailure(
        Player* bot, MovementManager* movement, Unit* target,
        uint64_t pathGenerationBefore)
    {
        if (!bot || !movement || !target)
            return false;

        bool freshAttempt = movement->GetPathAttemptGeneration() !=
            pathGenerationBefore;
        bool failed = movement->GetLastPathFailure() != BotPathFailure::None;
        uint64_t targetKey = target->GetGUID().GetRawValue();
        if (!_objectiveTargetPathFailures.Observe(targetKey, freshAttempt,
            failed))
        {
            return false;
        }

        if (target->GetVictim() == bot || target->IsInCombatWith(bot))
            return false;

        constexpr uint32_t ObjectiveTargetSuppressionSeconds = 60;
        _suppressedObjectiveTargets[targetKey] =
            Helper::MonotonicSeconds() + ObjectiveTargetSuppressionSeconds;
        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' skipped objective target '{}' (Entry {}, GUID {}) for {} seconds after {} fresh path failures: {} (flags {})",
                bot->GetName(), target->GetName(), target->GetEntry(),
                target->GetGUID().GetCounter(),
                ObjectiveTargetSuppressionSeconds,
                _objectiveTargetPathFailures.GetFailureCount(),
                movement->GetLastPathFailureName(),
                movement->GetLastPathFlags());
        }
        movement->Stop();
        _objectiveTargetGuid.Clear();
        _creatureSearchCooldownMs = 0;
        _lastProgressSubPath = SubPath_None;
        _objectiveTargetPathFailures.Reset();
        _objectiveTargetApproachProgress.Reset();
        return true;
    }

    bool ProgressQuestAction::HandleObjectiveTargetApproachStall(
        Player* bot, MovementManager* movement, Unit* target,
        uint64_t pathGenerationBefore, uint32_t deltaMs)
    {
        if (!bot || !movement || !target)
            return false;

        bool freshAttempt = movement->GetPathAttemptGeneration() !=
            pathGenerationBefore;
        uint64_t targetKey = target->GetGUID().GetRawValue();
        bool engaged = target->GetVictim() == bot ||
            target->IsInCombatWith(bot);
        if (!_objectiveTargetApproachProgress.Observe(targetKey,
            bot->GetDistance(target), target->GetHealth(), engaged,
            freshAttempt, deltaMs))
        {
            return false;
        }

        if (engaged)
            return false;

        constexpr uint32_t ObjectiveTargetSuppressionSeconds = 60;
        uint32_t stalledMs =
            _objectiveTargetApproachProgress.GetNoProgressMs();
        uint32_t pathAttempts =
            _objectiveTargetApproachProgress.GetPathAttemptCount();
        _suppressedObjectiveTargets[targetKey] =
            Helper::MonotonicSeconds() + ObjectiveTargetSuppressionSeconds;

        Diagnostics::StructuredEvent event;
        event.event = "quest_objective_target_suppressed";
        event.goal = "ProgressQuest";
        event.action = GetName();
        event.questId = _lockedQuestId;
        event.requestX = target->GetPositionX();
        event.requestY = target->GetPositionY();
        event.requestZ = target->GetPositionZ();
        event.pathFailure = movement->GetLastPathFailureName();
        event.pathFlags = movement->GetLastPathFlags();
        event.pathAttemptGeneration = movement->GetPathAttemptGeneration();
        std::ostringstream details;
        details << "reason=approach_no_progress;entry="
            << target->GetEntry() << ";guid="
            << target->GetGUID().GetCounter() << ";stalled_ms="
            << stalledMs << ";path_attempts=" << pathAttempts
            << ";distance=" << bot->GetDistance(target)
            << ";target_health=" << target->GetHealth();
        event.details = details.str();
        Diagnostics::StructuredEventLog::Write(bot, std::move(event));

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' skipped objective target '{}' (Entry {}, GUID {}) for {} seconds after {}ms and {} path attempts produced no damage, combat engagement, or meaningful distance progress",
                bot->GetName(), target->GetName(), target->GetEntry(),
                target->GetGUID().GetCounter(),
                ObjectiveTargetSuppressionSeconds, stalledMs, pathAttempts);
        }
        movement->Stop();
        _objectiveTargetGuid.Clear();
        _creatureSearchCooldownMs = 0;
        _lastProgressSubPath = SubPath_None;
        _objectiveTargetPathFailures.Reset();
        _objectiveTargetApproachProgress.Reset();
        return true;
    }

    bool ProgressQuestAction::FindAndEngageObjectiveMob(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active,
        Quest const* /*qTemplate*/, float searchRadius, uint32_t deltaMs)
    {
        std::unordered_set<uint32_t> validEntries;
        for (const auto& objective : active.objectives)
        {
            if (objective.currentCount >= objective.requiredCount)
                continue;
            if (!objective.combatLevelSuitable)
                continue;
            if ((objective.type == Blackboard::QuestObjectiveType::KillCreature ||
                 objective.type == Blackboard::QuestObjectiveType::TalkToCreature) && objective.targetEntry)
                validEntries.insert(objective.targetEntry);
            else if (objective.type == Blackboard::QuestObjectiveType::CollectItem && objective.itemId)
            {
                if (objective.vendorPurchase)
                    continue;
                for (const auto& source : Cache::BotCache::GetItemLootSources(objective.itemId))
                    if (source.type == Cache::LootSourceType::Creature)
                        validEntries.insert(source.entry);
            }
        }
        if (validEntries.empty())
            return false;

        if (_objectiveTargetGuid && _objectiveTargetGuid.IsCreatureOrVehicle())
        {
            if (Creature* target = bot->GetMap()->GetCreature(_objectiveTargetGuid))
            {
                if (target->IsAlive() &&
                    !IsObjectiveTargetSuppressed(target) &&
                    validEntries.find(target->GetEntry()) != validEntries.end())
                    return HandleCombatTarget(bot, movement, blackboard, target, deltaMs);
            }
            _objectiveTargetGuid.Clear();
        }

        if (_creatureSearchCooldownMs > 0)
            return false;
        _creatureSearchCooldownMs = 250;

        std::list<Creature*> nearbyMobs;
        Trinity::AnyUnitInObjectRangeCheck check(bot, searchRadius);
        Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, nearbyMobs, check);
        Cell::VisitGridObjects(bot, searcher, searchRadius);

        Creature* bestTarget = nullptr;
        float bestDistance = searchRadius + 1.0f;
        for (Creature* mob : nearbyMobs)
        {
            if (!mob || !mob->IsAlive() || !mob->isTargetableForAttack() || !bot->IsValidAttackTarget(mob) ||
                validEntries.find(mob->GetEntry()) == validEntries.end())
                continue;
            bool defendingSelf = mob->GetVictim() == bot || mob->IsInCombatWith(bot);
            if (!defendingSelf && IsObjectiveTargetSuppressed(mob))
                continue;
            if (!defendingSelf && !Helper::IsQuestObjectiveCreatureSuitable(
                bot->GetLevel(), mob->GetLevel(),
                Config::BotConfig::GetQuestMaxLevelsAboveBot()))
            {
                continue;
            }
            float distance = bot->GetDistance(mob);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTarget = mob;
            }
        }

        return bestTarget && HandleCombatTarget(bot, movement, blackboard, bestTarget, deltaMs);
    }

    bool ProgressQuestAction::FindAndUseObjectiveGameObject(Player* bot, MovementManager* movement,
        const Blackboard::ActiveQuest& active)
    {
        std::unordered_set<uint32_t> validEntries;
        std::unordered_set<uint32_t> lootEntries;
        for (const auto& objective : active.objectives)
        {
            if (objective.currentCount >= objective.requiredCount)
                continue;
            if (objective.type == Blackboard::QuestObjectiveType::InteractGameObject)
            {
                uint32_t interactionEntry = objective.interactionEntry != 0
                    ? objective.interactionEntry : objective.targetEntry;
                if (interactionEntry)
                    validEntries.insert(interactionEntry);
            }
            else if (objective.type == Blackboard::QuestObjectiveType::CollectItem && objective.itemId)
            {
                if (objective.vendorPurchase)
                    continue;
                for (const auto& source : Cache::BotCache::GetItemLootSources(objective.itemId))
                    if (source.type == Cache::LootSourceType::GameObject)
                    {
                        validEntries.insert(source.entry);
                        lootEntries.insert(source.entry);
                    }
            }
        }
        if (validEntries.empty())
            return false;

        GameObject* target = nullptr;
        if (_objectiveTargetGuid && _objectiveTargetGuid.IsGameObject())
            target = bot->GetMap()->GetGameObject(_objectiveTargetGuid);

        if (!target || !target->isSpawned() || validEntries.find(target->GetEntry()) == validEntries.end())
        {
            if (_gameObjectSearchCooldownMs > 0)
                return false;
            _gameObjectSearchCooldownMs = 250;
            std::list<GameObject*> objects;
            ObjectiveGameObjectCheck check{ bot, validEntries, 105.0f };
            Trinity::GameObjectListSearcher<ObjectiveGameObjectCheck> searcher(bot, objects, check);
            Cell::VisitGridObjects(bot, searcher, 105.0f);
            if (!objects.empty())
            {
                objects.sort([bot](GameObject* a, GameObject* b) { return bot->GetDistance(a) < bot->GetDistance(b); });
                target = objects.front();
                _objectiveTargetGuid = target->GetGUID();
            }
        }

        if (!target)
            return false;
        if (bot->GetDistance(target) > ObjectiveInteractionRange)
        {
            TryMoveToObjective(movement, target->GetPositionX(),
                target->GetPositionY(), target->GetPositionZ(),
                "quest_gameobject");
            return true;
        }

        movement->Stop();
        // Let the normal LootAction own chest/container inventory transfer.
        // ProgressQuest only brings the bot into range; the brain will select
        // Loot on its next Think tick while preserving this quest context.
        if (lootEntries.find(target->GetEntry()) != lootEntries.end())
            return true;

        if (_interactionRetryTimerMs == 0)
        {
            bot->SetFacingToObject(target);
            bool usedQuestSourceItem = false;
            uint32_t sourceItemId = 0;
            const Blackboard::QuestObjectiveData* sourceObjective = nullptr;
            for (const Blackboard::QuestObjectiveData& objective : active.objectives)
            {
                uint32_t interactionEntry = objective.interactionEntry != 0
                    ? objective.interactionEntry : objective.targetEntry;
                if (objective.type == Blackboard::QuestObjectiveType::InteractGameObject &&
                    interactionEntry == target->GetEntry() && objective.sourceItemId != 0)
                {
                    sourceItemId = objective.sourceItemId;
                    sourceObjective = &objective;
                    break;
                }
            }
            Item* sourceItem = sourceItemId ? bot->GetItemByEntry(sourceItemId) : nullptr;

            if (sourceObjective && sourceItem)
            {
                SpellCastTargets targets;
                if (!sourceObjective->sourceSpellTargetsEntity)
                    targets.SetUnitTarget(bot);
                else
                    targets.SetGOTarget(target);
                usedQuestSourceItem = Helper::SpellUtils::TryUseResolvedQuestItem(
                    bot, sourceItem, sourceObjective->sourceSpellId, targets);
            }
            else if (!sourceObjective)
                target->Use(bot);

            _interactionRetryTimerMs = 2000;
            _lastProgressSubPath = SubPath_Interact;
            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                if (usedQuestSourceItem)
                    TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' used source Item {} on objective GameObject Entry {} for quest {}",
                        bot->GetName(), sourceItemId, target->GetEntry(), _lockedQuestId);
                else
                    TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' used objective GameObject Entry {} for quest {}",
                        bot->GetName(), target->GetEntry(), _lockedQuestId);
            }
        }
        return true;
    }

    bool ProgressQuestAction::HandleVendorPurchaseObjective(Player* bot,
        MovementManager* movement, const Blackboard::ActiveQuest& active)
    {
        const Blackboard::QuestObjectiveData* objective = nullptr;
        Creature* vendor = nullptr;
        for (const auto& candidate : active.objectives)
        {
            if (candidate.vendorPurchase && candidate.itemId != 0 &&
                candidate.currentCount < candidate.requiredCount)
            {
                // Sense data can lag one brain tick behind a successful
                // purchase. Do not buy the same objective twice while waiting
                // for the blackboard count to refresh.
                if (bot->GetItemCount(candidate.itemId, false) >= candidate.requiredCount)
                    return true;
                Creature* nearbyVendor = Helper::NpcUtils::FindNearbyCreatureByEntry(
                    bot, candidate.targetEntry, 30.0f);
                if (nearbyVendor)
                {
                    objective = &candidate;
                    vendor = nearbyVendor;
                    break;
                }
            }
        }
        if (!objective || !vendor)
            return false;
        if (!vendor->IsAlive() ||
            !vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR) ||
            !Cache::BotCache::VendorSellsItem(vendor->GetEntry(), objective->itemId))
        {
            Finish(ActionOutcome::Unsupported,
                "resolved quest-item vendor does not offer the required item",
                FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return true;
        }

        Helper::InteractionStatus interactionStatus = Helper::NpcUtils::GetInteractionStatus(
            bot, vendor, Constants::VendorInteractionRange);
        if (interactionStatus == Helper::InteractionStatus::NeedsMovement)
        {
            TryMoveToObjective(movement, vendor->GetPositionX(),
                vendor->GetPositionY(), vendor->GetPositionZ(),
                "quest_vendor");
            return true;
        }
        if (interactionStatus != Helper::InteractionStatus::Ready)
            return false;

        uint32_t liveCount = bot->GetItemCount(objective->itemId, false);
        if (liveCount >= objective->requiredCount)
        {
            Finish(ActionOutcome::Succeeded, "quest vendor item requirement already met");
            return true;
        }
        uint32_t missingCount = objective->requiredCount - liveCount;
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(objective->itemId);
        if (!itemTemplate)
        {
            Finish(ActionOutcome::Unsupported, "quest vendor item template is missing",
                FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return true;
        }

        VendorItemData const* vendorItems = vendor->GetVendorItems();
        uint32_t vendorSlot = 0;
        while (vendorItems && vendorSlot < vendorItems->GetItemCount())
        {
            VendorItem const* vendorItem = vendorItems->GetItem(vendorSlot);
            if (vendorItem && vendorItem->item == objective->itemId)
                break;
            ++vendorSlot;
        }
        if (!vendorItems || vendorSlot >= vendorItems->GetItemCount())
        {
            Finish(ActionOutcome::Unsupported,
                "required quest item is absent from the live vendor inventory",
                FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return true;
        }

        uint32_t buyCount = std::max<uint32_t>(1, itemTemplate->BuyCount);
        uint32_t purchaseBundles = (missingCount + buyCount - 1) / buyCount;
        uint8_t purchaseCount = static_cast<uint8_t>(std::min<uint32_t>(255, purchaseBundles));
        uint32_t purchaseUnits = purchaseCount * buyCount;
        VendorItem const* vendorItem = vendorItems->GetItem(vendorSlot);
        auto finishPurchaseIssue = [this](QuestVendorPurchaseIssue issue,
            std::string reason, uint32_t restockSeconds = 0) {
            QuestVendorPurchaseDecision decision =
                SelectQuestVendorPurchaseRecovery(issue, restockSeconds);
            _retryDelaySeconds = decision.retryDelaySeconds;
            Finish(decision.outcome, std::move(reason), decision.category,
                decision.directive);
        };

        if (!vendorItem)
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::InvalidVendorData,
                "required quest item resolved to an invalid live vendor slot");
            return true;
        }

        bool classIneligible =
            !(itemTemplate->AllowableClass & bot->GetClassMask()) &&
            itemTemplate->Bonding == BIND_WHEN_PICKED_UP && !bot->IsGameMaster();
        bool factionIneligible = !bot->IsGameMaster() &&
            ((itemTemplate->HasFlag(ITEM_FLAG2_FACTION_HORDE) &&
                bot->GetTeam() == ALLIANCE) ||
             (itemTemplate->HasFlag(ITEM_FLAG2_FACTION_ALLIANCE) &&
                bot->GetTeam() == HORDE));
        if (classIneligible || factionIneligible ||
            !bot->MatchRaceClassMask(vendorItem->raceMask,
                vendorItem->classMask))
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::IneligibleCharacter,
                "required vendor quest item is not available to this bot's class, race, or faction");
            return true;
        }

        if (!sConditionMgr->IsObjectMeetingVendorItemConditions(
            vendor->GetEntry(), objective->itemId, bot, vendor))
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::UnmetVendorCondition,
                "required vendor quest item has an unmet authored condition; retaining the quest and retrying after other progression");
            return true;
        }

        if (vendorItem->maxcount != 0 &&
            vendor->GetVendorItemCurrentCount(vendorItem) < purchaseUnits)
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::OutOfStock,
                "required vendor quest item is temporarily out of stock; retaining the quest until the vendor restocks",
                vendorItem->incrtime);
            return true;
        }

        if (itemTemplate->RequiredReputationFaction &&
            uint32_t(bot->GetReputationRank(
                itemTemplate->RequiredReputationFaction)) <
                itemTemplate->RequiredReputationRank)
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::ReputationRequirement,
                "required vendor quest item needs a higher reputation rank; retaining the quest without treating a character level as the remedy");
            return true;
        }

        if (vendorItem->ExtendedCost)
        {
            ItemExtendedCostEntry const* extendedCost =
                sItemExtendedCostStore.LookupEntry(vendorItem->ExtendedCost);
            if (!extendedCost)
            {
                finishPurchaseIssue(QuestVendorPurchaseIssue::InvalidVendorData,
                    "required vendor quest item references an invalid extended cost");
                return true;
            }

            if (bot->GetHonorPoints() < extendedCost->HonorPoints * purchaseCount)
            {
                finishPurchaseIssue(QuestVendorPurchaseIssue::ExtendedCostRequirement,
                    "required vendor quest item needs more honor points; retaining the quest without level deferral");
                return true;
            }
            if (bot->GetArenaPoints() < extendedCost->ArenaPoints * purchaseCount)
            {
                finishPurchaseIssue(QuestVendorPurchaseIssue::ExtendedCostRequirement,
                    "required vendor quest item needs more arena points; retaining the quest without level deferral");
                return true;
            }
            for (uint8_t costIndex = 0;
                costIndex < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++costIndex)
            {
                uint32_t costItem = extendedCost->ItemID[costIndex];
                uint32_t costCount = extendedCost->ItemCount[costIndex] *
                    purchaseCount;
                if (costItem && !bot->HasItemCount(costItem, costCount))
                {
                    finishPurchaseIssue(QuestVendorPurchaseIssue::ExtendedCostRequirement,
                        "required vendor quest item needs additional currency or turn-in items; retaining the quest without level deferral");
                    return true;
                }
            }
            if (bot->GetMaxPersonalArenaRatingRequirement(
                    extendedCost->ArenaBracket) <
                extendedCost->RequiredArenaRating)
            {
                finishPurchaseIssue(QuestVendorPurchaseIssue::ExtendedCostRequirement,
                    "required vendor quest item needs a higher arena rating; retaining the quest without level deferral");
                return true;
            }
        }

        uint32_t requiredMoney = 0;
        bool goldRequired = itemTemplate->HasFlag(
            ITEM_FLAG2_DONT_IGNORE_BUY_PRICE) || !vendorItem->ExtendedCost;
        if (goldRequired &&
            itemTemplate->BuyPrice > 0)
        {
            uint64_t undiscountedMoney =
                static_cast<uint64_t>(itemTemplate->BuyPrice) * purchaseCount;
            uint64_t discountedMoney = static_cast<uint64_t>(std::floor(
                static_cast<double>(undiscountedMoney) *
                bot->GetReputationPriceDiscount(vendor)));
            requiredMoney = static_cast<uint32_t>(std::min<uint64_t>(
                discountedMoney, MAX_MONEY_AMOUNT));
            if (!bot->HasEnoughMoney(requiredMoney))
            {
                uint32_t missingMoney = requiredMoney - bot->GetMoney();
                finishPurchaseIssue(QuestVendorPurchaseIssue::InsufficientMoney,
                    "required vendor quest item costs " +
                    std::to_string(requiredMoney) + " copper, but the bot has " +
                    std::to_string(bot->GetMoney()) + " (missing " +
                    std::to_string(missingMoney) +
                    "); retaining the quest while other work earns funds");
                return true;
            }
        }

        ItemPosCountVec destination;
        InventoryResult storeResult = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT,
            destination, objective->itemId, purchaseUnits);
        if (storeResult != EQUIP_ERR_OK)
        {
            _inventoryCapacityFailure = true;
            _retryDelaySeconds = 120;
            Finish(ActionOutcome::Blocked,
                "inventory cannot store the required vendor quest item",
                FailureCategory::InventoryCapacity, RecoveryDirective::RetryLater);
            return true;
        }

        movement->Stop();
        Helper::NpcUtils::PrepareCreatureInteraction(bot, vendor);
        // Clear any multivendor selection left by a previous gossip session;
        // this objective is purchasing from the creature's authored base list.
        WorldSession* session = bot->GetSession();
        if (!session)
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::UnexpectedRejection, "bot session was unavailable");
            return true;
        }
        session->SetCurrentVendor(0);
        uint32_t moneyBefore = bot->GetMoney();
        uint32_t itemCountBefore = bot->GetItemCount(objective->itemId, false);
        bot->BuyItemFromVendorSlot(vendor->GetGUID(), vendorSlot,
            objective->itemId, purchaseCount, NULL_BAG, NULL_SLOT);
        uint32_t itemCountAfter = bot->GetItemCount(objective->itemId, false);
        if (itemCountAfter <= itemCountBefore)
        {
            finishPurchaseIssue(QuestVendorPurchaseIssue::UnexpectedRejection,
                "vendor purchase failed after stock, price, eligibility, condition, and inventory checks passed; treating this vendor objective as unsupported for the session");
            return true;
        }

        _progressActivitySignature ^= (static_cast<uint64_t>(objective->itemId) << 32) |
            itemCountAfter;
        _interactionRetryTimerMs = 2000;
        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' purchased {} x{} from vendor '{}' (Entry {}) for quest {} (cost: {} copper)",
                bot->GetName(), itemTemplate->Name1, itemCountAfter - itemCountBefore,
                vendor->GetName(), vendor->GetEntry(), active.questId,
                moneyBefore - bot->GetMoney());
        }
        return true;
    }

    bool ProgressQuestAction::HandleDeathKnightRunebladeObjective(Player* bot,
        MovementManager* movement, const Blackboard::ActiveQuest& active)
    {
        if (!bot || !movement || active.questId != EmblazonedRunebladeQuestId ||
            bot->GetMapId() != 609 || bot->GetClass() != CLASS_DEATH_KNIGHT ||
            bot->GetItemCount(RunebladedSwordItemId, false) > 0)
        {
            return false;
        }

        Item* battleWornSword = bot->GetItemByEntry(BattleWornSwordItemId);
        if (!battleWornSword)
        {
            GameObject* target = bot->FindNearestGameObject(
                BattleWornSwordGameObjectEntry, 105.0f);
            if (!target || !target->isSpawned() ||
                target->GetGoState() != GO_STATE_READY)
            {
                return false;
            }

            _objectiveTargetGuid = target->GetGUID();
            if (bot->GetDistance(target) > ObjectiveInteractionRange)
            {
                TryMoveToObjective(movement, target->GetPositionX(),
                    target->GetPositionY(), target->GetPositionZ(),
                    "quest_runeblade_gameobject");
                _lastProgressSubPath = SubPath_Travel;
                return true;
            }

            // LootAction owns the inventory transfer on the next brain tick.
            movement->Stop();
            _lastProgressSubPath = SubPath_Interact;
            return true;
        }

        Creature* runeforge = Helper::NpcUtils::FindNearbyCreatureByEntry(
            bot, RuneforgeCreatureEntry, 105.0f);
        if (!runeforge)
            return false;

        _objectiveTargetGuid = runeforge->GetGUID();
        if (bot->GetDistance(runeforge) > 8.0f)
        {
            TryMoveToObjective(movement, runeforge->GetPositionX(),
                runeforge->GetPositionY(), runeforge->GetPositionZ(),
                "quest_runeforge");
            _lastProgressSubPath = SubPath_Travel;
            return true;
        }

        movement->Stop();
        if (_interactionRetryTimerMs == 0 &&
            !bot->IsNonMeleeSpellCast(false) &&
            bot->CanUseItem(battleWornSword) == EQUIP_ERR_OK)
        {
            SpellCastTargets targets;
            targets.SetUnitTarget(runeforge);
            bot->CastItemUseSpell(battleWornSword, targets, 0, 0);
            _interactionRetryTimerMs = 5000;
            _lastProgressSubPath = SubPath_Interact;
            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' used Battle-worn Sword {} at runeforge {} for quest {}",
                    bot->GetName(), BattleWornSwordItemId,
                    runeforge->GetGUID().GetCounter(), active.questId);
            }
        }
        return true;
    }

    uint64_t ProgressQuestAction::MakeObjectiveDestinationKey(float x,
        float y, float z)
    {
        uint64_t hash = 1469598103934665603ULL;
        auto mix = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(x * 2.0f))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(y * 2.0f))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(z * 2.0f))));
        return hash;
    }

    bool ProgressQuestAction::TryMoveToObjective(MovementManager* movement,
        float x, float y, float z, const char* pathSource)
    {
        if (!movement)
            return false;
        movement->SetDiagnosticPathSource(pathSource ? pathSource :
            "quest_objective");
        uint64_t pathGeneration = movement->GetPathAttemptGeneration();
        bool started = movement->MoveTo(x, y, z,
            BotMovementState::Moving, false);
        bool freshAttempt = movement->GetPathAttemptGeneration() !=
            pathGeneration;
        bool failed = !started &&
            movement->GetLastPathFailure() != BotPathFailure::None;
        bool failureLimitReached =
            _objectiveDestinationPathFailures.Observe(
                MakeObjectiveDestinationKey(x, y, z), freshAttempt, failed);
        if (started)
        {
            return true;
        }

        if (failureLimitReached)
        {
            std::string reason = "quest objective movement failed after 3 attempts: ";
            reason += movement->GetLastPathFailureName();
            reason += " (path flags ";
            reason += std::to_string(movement->GetLastPathFlags());
            reason += ", source ";
            reason += pathSource ? pathSource : "quest_objective";
            reason += ")";
            Finish(ActionOutcome::Blocked, std::move(reason),
                FailureCategory::Navigation, RecoveryDirective::RetryLater);
        }
        return false;
    }

    void ProgressQuestAction::WanderObjectiveArea(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active,
        Quest const* qTemplate, float searchRadius, uint32_t deltaMs)
    {
        if (!active.hasTargetPosition)
        {
            movement->Stop();
            Finish(ActionOutcome::Unsupported,
                active.hasUnsupportedObjective ? "quest contains an unsupported objective" : "no objective location or spawn could be resolved",
                FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return;
        }

        float distanceSq = Helper::DistanceSq(blackboard.self.x, blackboard.self.y, blackboard.self.z,
            active.targetPosition.x, active.targetPosition.y, active.targetPosition.z);
        bool shouldTravel = _lastProgressSubPath == SubPath_Wander ? distanceSq > 900.0f : distanceSq > 100.0f;
        if (shouldTravel)
        {
            if (_lastProgressSubPath != SubPath_Travel)
            {
                _lastProgressSubPath = SubPath_Travel;
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' traveling to objective area ({:.1f}, {:.1f}, {:.1f}) for quest {} ('{}')",
                        bot->GetName(), active.targetPosition.x, active.targetPosition.y, active.targetPosition.z,
                        active.questId, qTemplate->GetTitle());
            }
            TryMoveToObjective(movement, active.targetPosition.x,
                active.targetPosition.y, active.targetPosition.z,
                "quest_objective_area");
            return;
        }

        _searchExpandTimerMs += deltaMs;
        if (_searchExpandTimerMs >= Constants::WanderPauseIntervalMs)
        {
            _searchExpandTimerMs = 0;
            if (_searchExpandCount < 3)
                ++_searchExpandCount;
        }

        if (_lastProgressSubPath != SubPath_Wander)
        {
            _lastProgressSubPath = SubPath_Wander;
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' searching a {:.0f}yd objective camp for quest {} ('{}')",
                    bot->GetName(), searchRadius, active.questId, qTemplate->GetTitle());
        }

        if (movement->GetState() == BotMovementState::Idle)
        {
            float targetX = 0.0f, targetY = 0.0f;
            Helper::GetRandomPointInAnnulus(active.targetPosition.x, active.targetPosition.y, 8.0f,
                std::max(12.0f, searchRadius), targetX, targetY);
            float targetZ = bot->GetMap()->GetHeight(bot->GetPhaseMask(), targetX, targetY,
                active.targetPosition.z, true);
            if (targetZ <= -50000.0f || targetZ >= 50000.0f)
                targetZ = active.targetPosition.z;
            TryMoveToObjective(movement, targetX, targetY, targetZ,
                "quest_objective_search");
        }
    }

    void ProgressQuestAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (_completed)
            return;
        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "bot or movement context became unavailable",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        // Consume the activity observed by the preceding action tick before
        // clearing the per-tick value. A changed target or target health is
        // real objective progress even though the quest counter is unchanged.
        uint64_t recentCombatActivity = _progressActivitySignature;
        _progressActivitySignature = 0;
        if (recentCombatActivity != 0)
        {
            if (recentCombatActivity != _lastCombatActivitySignature)
            {
                _noProgressTimerMs = 0;
                _targetAcquireTimerMs = 0;
            }
            _lastCombatActivitySignature = recentCombatActivity;
        }
        else
        {
            _lastCombatActivitySignature = 0;
        }

        _sourceRecoveryCooldownMs = _sourceRecoveryCooldownMs > deltaMs ? _sourceRecoveryCooldownMs - deltaMs : 0;
        _interactionRetryTimerMs = _interactionRetryTimerMs > deltaMs ? _interactionRetryTimerMs - deltaMs : 0;
        _creatureSearchCooldownMs = _creatureSearchCooldownMs > deltaMs ? _creatureSearchCooldownMs - deltaMs : 0;
        _gameObjectSearchCooldownMs = _gameObjectSearchCooldownMs > deltaMs ? _gameObjectSearchCooldownMs - deltaMs : 0;

        const Blackboard::ActiveQuest* active = nullptr;
        for (const auto& candidate : blackboard.quest.activeQuests)
        {
            if (candidate.questId == _lockedQuestId)
            {
                active = &candidate;
                break;
            }
        }

        QuestStatus status = bot->GetQuestStatus(_lockedQuestId);
        if (status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_NONE)
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }
        if (!active)
        {
            Finish(status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_NONE
                ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure,
                status == QUEST_STATUS_INCOMPLETE ? "selected quest disappeared from the blackboard" : "");
            return;
        }

        Quest const* qTemplate = sObjectMgr->GetQuestTemplate(active->questId);
        if (!qTemplate)
        {
            Finish(ActionOutcome::Unsupported, "quest template is missing",
                FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return;
        }
        bool hasIncompleteUnsupportedObjective = std::any_of(
            active->objectives.begin(), active->objectives.end(),
            [](const Blackboard::QuestObjectiveData& objective) {
                return objective.type ==
                    Blackboard::QuestObjectiveType::Unsupported &&
                    objective.currentCount < objective.requiredCount;
            });
        if (hasIncompleteUnsupportedObjective)
        {
            movement->Stop();
            Finish(ActionOutcome::Unsupported,
                "quest contains an unresolved or unsafe item-use objective",
                FailureCategory::ContentUnsupported,
                RecoveryDirective::RetryLater);
            return;
        }
        uint64_t progressSignature = CalculateProgressSignature(*active);
        if (_lastProgressSignature == 0 || progressSignature != _lastProgressSignature)
        {
            _lastProgressSignature = progressSignature;
            _noProgressTimerMs = 0;
            _targetAcquireTimerMs = 0;
            _searchExpandCount = 0;
            _objectiveTargetGuid.Clear();
            _objectiveTargetPathFailures.Reset();
            _objectiveTargetApproachProgress.Reset();
        }
        else
        {
            // Reaching a known objective camp is navigational progress. Do
            // not spend the objective-search timeout while crossing the map;
            // MovementManager and StuckDetector own travel failures.
            bool travellingToKnownObjective = active->hasTargetPosition &&
                movement->GetState() != BotMovementState::Idle &&
                Helper::DistanceSq(blackboard.self.x, blackboard.self.y, blackboard.self.z,
                    active->targetPosition.x, active->targetPosition.y,
                    active->targetPosition.z) > 900.0f;
            if (!travellingToKnownObjective)
            {
                _noProgressTimerMs += deltaMs;
                _targetAcquireTimerMs += deltaMs;
            }
        }

        if (_noProgressTimerMs >= ObjectiveNoProgressTimeoutMs)
        {
            movement->Stop();
            Finish(ActionOutcome::Blocked, "no objective counter or inventory progress for 120 seconds",
                FailureCategory::ProgressionDifficulty, RecoveryDirective::GrindUntilLevel);
            return;
        }

        if (_sourceRecoveryCooldownMs == 0 && qTemplate->GetSrcItemId() != 0)
        {
            uint32_t itemId = qTemplate->GetSrcItemId();
            uint32_t count = qTemplate->GetSrcItemCount() == 0 ? 1 : qTemplate->GetSrcItemCount();
            if (bot->GetItemCount(itemId, false) < count)
            {
                bot->GiveQuestSourceItem(qTemplate);
                _sourceRecoveryCooldownMs = 1000;
            }
        }

        // Always defend against the unit currently attacking the bot. Quest
        // context survives this combat and resumes afterwards.
        ObjectGuid threatGuid = blackboard.combat.primaryAttackerGuid;
        if (!threatGuid)
            threatGuid = blackboard.party.groupTargetGuid;
        if (!threatGuid)
            threatGuid = blackboard.combat.currentTargetGuid;
        if (threatGuid)
        {
            if (Unit* threat = ObjectAccessor::GetUnit(*bot, threatGuid))
            {
                if (threat->IsAlive() && HandleCombatTarget(bot, movement, blackboard, threat, deltaMs))
                    return;
            }
        }

        // The blackboard may not report a proactively selected quest mob as
        // a combat target until its first hit lands. Keep that live selection
        // under combat ownership during the approach and opening cast.
        if (_objectiveTargetGuid && _objectiveTargetGuid.IsCreatureOrVehicle())
        {
            Creature* retainedTarget = bot->GetMap()->GetCreature(_objectiveTargetGuid);
            if (retainedTarget && retainedTarget->IsAlive() &&
                HandleCombatTarget(bot, movement, blackboard, retainedTarget, deltaMs))
            {
                return;
            }
            _objectiveTargetGuid.Clear();
        }

        ObjectiveContext objCtx{
            bot, movement, blackboard, *active, deltaMs,
            [this, movement](float x, float y, float z,
                const char* pathSource) {
                return TryMoveToObjective(movement, x, y, z, pathSource);
            }
        };

        if (HandleDeathKnightRunebladeObjective(bot, movement, *active))
            return;

        if (HandleVendorPurchaseObjective(bot, movement, *active))
            return;

        // Exploration Objectives
        auto expOutcome = ExplorationObjectiveHandler::TryHandle(objCtx);
        if (expOutcome.result == ObjectiveResult::Handled) return;
        if (expOutcome.result == ObjectiveResult::Completed)
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }

        // Scripted Talk / Delivery Objectives
        auto talkOutcome = TalkDeliveryObjectiveHandler::TryHandle(objCtx, _interactionRetryTimerMs, _targetAcquireTimerMs);
        if (talkOutcome.result == ObjectiveResult::Handled) return;
        if (talkOutcome.result == ObjectiveResult::Completed)
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }
        if (talkOutcome.result == ObjectiveResult::Failed)
        {
            Finish(ActionOutcome::Unsupported, talkOutcome.failureReason,
                FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return;
        }

        // All Objectives Complete Check
        bool allComplete = std::all_of(active->objectives.begin(), active->objectives.end(),
            [](const Blackboard::QuestObjectiveData& objective) {
                return objective.currentCount >= objective.requiredCount;
            });

        if (allComplete)
        {
            movement->Stop();
            if (bot->CanCompleteQuest(active->questId))
                bot->CompleteQuest(active->questId);
            if (bot->GetQuestStatus(active->questId) == QUEST_STATUS_COMPLETE)
                Finish(ActionOutcome::Succeeded);
            else if (active->objectives.empty() && !active->isTalkOrTravelOnly)
                Finish(ActionOutcome::Unsupported, "quest has no automatable objectives and core still reports it incomplete",
                    FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
            return;
        }

        // CastOnCreature Objectives
        auto castOutcome = CreditObjectiveHandler::TryCastOnCreature(objCtx, _interactionRetryTimerMs);
        if (castOutcome.result == ObjectiveResult::Handled) return;

        // Peaceful TalkToCreature Objectives
        auto talkCreatureOutcome = CreditObjectiveHandler::TryTalkToCreature(objCtx, _interactionRetryTimerMs);
        if (talkCreatureOutcome.result == ObjectiveResult::Handled) return;

        if (FindAndUseObjectiveGameObject(bot, movement, *active))
            return;

        float searchRadius = std::min(105.0f, 30.0f + (_searchExpandCount * 25.0f));
        if (FindAndEngageObjectiveMob(bot, movement, blackboard, *active, qTemplate, searchRadius, deltaMs))
            return;

        // Nearby objectives and retained combat targets own movement before
        // the static objective-area route. Otherwise a ranged class can begin
        // approaching a mob here, then have WorldTravel replace that command
        // on the next tick before its opener establishes combat.
        if (active->hasTargetPosition &&
            (_worldTravel.IsActive() ||
             Travel::WorldTravel::NeedsTravel(bot, active->targetPosition, 20.0f)))
        {
            if (_lastProgressSubPath != SubPath_Travel)
                _lastProgressSubPath = SubPath_Travel;
            Travel::TravelResult travelResult = _worldTravel.Update(bot, movement,
                active->targetPosition, deltaMs, _dangerAreas,
                blackboard.spatial.hostileGuids);
            if (travelResult == Travel::TravelResult::Failed)
                Finish(ActionOutcome::Blocked, _worldTravel.GetFailureReason(),
                    FailureCategory::Navigation, RecoveryDirective::RetryLater);
            return;
        }

        if (_targetAcquireTimerMs >= InteractionNoProgressTimeoutMs && !active->hasTargetPosition)
        {
            Finish(ActionOutcome::Blocked, "no live objective target was found for 30 seconds",
                FailureCategory::ProgressionDifficulty, RecoveryDirective::GrindUntilLevel);
            return;
        }
        WanderObjectiveArea(bot, movement, blackboard, *active, qTemplate, searchRadius, deltaMs);
    }

    void ProgressQuestAction::Stop(Player* bot, MovementManager* movement)
    {
        _worldTravel.Stop(bot, movement);
        _objectiveTargetPathFailures.Reset();
        _objectiveDestinationPathFailures.Reset();
        _objectiveTargetApproachProgress.Reset();
    }
}
