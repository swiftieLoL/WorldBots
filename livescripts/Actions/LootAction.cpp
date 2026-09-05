#include "LootAction.h"
#include "Actions/LootCapacityRetryPolicy.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Globals/ObjectAccessor.h"
#include "Creature.h"
#include "GameObject.h"
#include "Corpse.h"
#include "Map.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Loot/Loot.h"
#include "Server/WorldSession.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include "DataStores/DBCStores.h"
#include "Helper/Constants.h"
#include "Helper/InventoryUtils.h"
#include "Helper/TimeUtils.h"
#include <algorithm>
#include <list>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace Actions
{
    struct SuppressedLootArea
    {
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float radius = 0.0f;
        uint32_t expirySec = 0;
    };

    static std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> s_ignoredLootExpiryByBot;
    static std::unordered_map<uint64_t,
        std::unordered_map<uint64_t, LootCapacityRetryPolicy::BlockedTarget>>
        s_inventoryBlockedLootByBot;
    static std::unordered_map<uint64_t, std::vector<SuppressedLootArea>> s_suppressedLootAreasByBot;

    static void PruneIgnoredLootGuids()
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        for (auto botIt = s_ignoredLootExpiryByBot.begin(); botIt != s_ignoredLootExpiryByBot.end(); )
        {
            auto& targets = botIt->second;
            for (auto targetIt = targets.begin(); targetIt != targets.end(); )
            {
                if (nowSec >= targetIt->second)
                    targetIt = targets.erase(targetIt);
                else
                    ++targetIt;
            }

            if (targets.empty())
                botIt = s_ignoredLootExpiryByBot.erase(botIt);
            else
                ++botIt;
        }
    }

    static void PruneSuppressedLootAreas()
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        for (auto botIt = s_suppressedLootAreasByBot.begin();
             botIt != s_suppressedLootAreasByBot.end(); )
        {
            auto& areas = botIt->second;
            areas.erase(std::remove_if(areas.begin(), areas.end(),
                [nowSec](const SuppressedLootArea& area) {
                    return nowSec >= area.expirySec;
                }), areas.end());
            botIt = areas.empty() ? s_suppressedLootAreasByBot.erase(botIt) : std::next(botIt);
        }
    }

    static void AddIgnoredLootGuid(Player* bot, uint64_t rawGuid, uint32_t durationSec = 180)
    {
        if (!bot)
            return;
        uint32_t expirySec = Helper::MonotonicSeconds() + durationSec;
        s_ignoredLootExpiryByBot[bot->GetGUID().GetRawValue()][rawGuid] = expirySec;
    }

    static bool IsLootGuidIgnored(Player* bot, uint64_t rawGuid, const std::set<uint64_t>& externalIgnored)
    {
        if (externalIgnored.find(rawGuid) != externalIgnored.end())
            return true;

        if (!bot)
            return false;

        auto botIt = s_ignoredLootExpiryByBot.find(bot->GetGUID().GetRawValue());
        if (botIt != s_ignoredLootExpiryByBot.end())
        {
            auto targetIt = botIt->second.find(rawGuid);
            if (targetIt != botIt->second.end() && Helper::MonotonicSeconds() < targetIt->second)
                return true;
        }
        return false;
    }

    static bool IsLootPositionIgnored(Player* bot, WorldObject* target)
    {
        if (!bot || !target)
            return false;

        auto botIt = s_suppressedLootAreasByBot.find(bot->GetGUID().GetRawValue());
        if (botIt == s_suppressedLootAreasByBot.end())
            return false;

        for (const SuppressedLootArea& area : botIt->second)
        {
            if (target->GetMapId() != area.mapId)
                continue;
            float dx = target->GetPositionX() - area.x;
            float dy = target->GetPositionY() - area.y;
            float dz = target->GetPositionZ() - area.z;
            if ((dx * dx + dy * dy + dz * dz) <= area.radius * area.radius)
                return true;
        }
        return false;
    }

    static void PruneInventoryBlockedLootGuids()
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        for (auto botIt = s_inventoryBlockedLootByBot.begin();
             botIt != s_inventoryBlockedLootByBot.end(); )
        {
            auto& targets = botIt->second;
            for (auto targetIt = targets.begin(); targetIt != targets.end(); )
                targetIt = nowSec >= targetIt->second.expirySec
                    ? targets.erase(targetIt) : std::next(targetIt);

            botIt = targets.empty() ? s_inventoryBlockedLootByBot.erase(botIt) : std::next(botIt);
        }
    }

    static void AddInventoryBlockedLootGuid(Player* bot, uint64_t rawGuid)
    {
        if (!bot || rawGuid == 0)
            return;
        constexpr uint32_t BlockedLootRetrySeconds = 180;
        s_inventoryBlockedLootByBot[bot->GetGUID().GetRawValue()][rawGuid] = {
            Helper::MonotonicSeconds() + BlockedLootRetrySeconds,
            Helper::InventoryUtils::CountFreeBagSlots(bot)
        };
    }

    static void RemoveInventoryBlockedLootGuid(Player* bot, uint64_t rawGuid)
    {
        if (!bot)
            return;
        auto botIt = s_inventoryBlockedLootByBot.find(bot->GetGUID().GetRawValue());
        if (botIt == s_inventoryBlockedLootByBot.end())
            return;
        botIt->second.erase(rawGuid);
        if (botIt->second.empty())
            s_inventoryBlockedLootByBot.erase(botIt);
    }

    static bool IsInventoryBlockedLootGuid(Player* bot, uint64_t rawGuid)
    {
        if (!bot)
            return false;
        auto botIt = s_inventoryBlockedLootByBot.find(bot->GetGUID().GetRawValue());
        return botIt != s_inventoryBlockedLootByBot.end() &&
            botIt->second.find(rawGuid) != botIt->second.end();
    }

    struct FindLootCheck
    {
        Player* const i_bot;
        float i_range;
        const std::set<uint64_t>& i_ignored;

        FindLootCheck(Player* bot, float range, const std::set<uint64_t>& ignored = {})
            : i_bot(bot), i_range(range), i_ignored(ignored) {}

        bool operator()(Creature* u)
        {
            if (!u || !u->isDead() || u->getDeathState() != CORPSE)
                return false;
            if (!u->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
                return false;
            if (i_bot->GetDistance(u) > i_range)
                return false;
            if (IsLootGuidIgnored(i_bot, u->GetGUID().GetRawValue(), i_ignored))
                return false;
            if (IsLootPositionIgnored(i_bot, u))
                return false;
            if (IsInventoryBlockedLootGuid(i_bot, u->GetGUID().GetRawValue()))
                return false;
            if (!i_bot->isAllowedToLoot(u))
                return false;
            return true;
        }

        bool operator()(GameObject* go)
        {
            if (!go || !go->isSpawned() || go->GetGoState() != GO_STATE_READY)
                return false;
            if (i_bot->GetDistance(go) > i_range)
                return false;
            if (IsLootGuidIgnored(i_bot, go->GetGUID().GetRawValue(), i_ignored))
                return false;
            if (IsLootPositionIgnored(i_bot, go))
                return false;
            if (IsInventoryBlockedLootGuid(i_bot, go->GetGUID().GetRawValue()))
                return false;

            // Only loot chests, gather nodes, and quest containers
            if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST || go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
            {
                uint32 lockId = go->GetGOInfo() ? go->GetGOInfo()->GetLockId() : 0;
                if (lockId)
                {
                    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
                    if (lockInfo)
                    {
                        for (uint8 i = 0; i < 8; ++i)
                        {
                            if (lockInfo->Type[i] == LOCK_KEY_SKILL)
                            {
                                uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                                if (skillId == SKILL_MINING || skillId == SKILL_HERBALISM)
                                {
                                    if (i_bot->HasSkill((SkillType)skillId))
                                        return true;
                                }
                            }
                            else if (lockInfo->Type[i] == LOCK_KEY_NONE)
                            {
                                return true;
                            }
                        }
                    }
                }
                else
                {
                    return true;
                }
            }

            return false;
        }
    };

    LootAction::LootAction(ObjectGuid corpseGuid)
        : _targetGuid(corpseGuid)
    {
    }

    void LootAction::SuppressTarget(Player* bot, ObjectGuid targetGuid, uint32_t durationSec)
    {
        if (!targetGuid)
            return;
        AddIgnoredLootGuid(bot, targetGuid.GetRawValue(), durationSec);
    }

    void LootAction::SuppressDangerousArea(Player* bot, ObjectGuid anchorGuid,
        float radius, uint32_t durationSec)
    {
        if (!bot || !bot->IsInWorld() || radius <= 0.0f)
            return;

        WorldObject* anchor = anchorGuid
            ? ObjectAccessor::GetWorldObject(*bot, anchorGuid) : nullptr;
        WorldObject* center = anchor ? anchor : bot;
        SuppressedLootArea area;
        area.mapId = center->GetMapId();
        area.x = center->GetPositionX();
        area.y = center->GetPositionY();
        area.z = center->GetPositionZ();
        area.radius = radius;
        area.expirySec = Helper::MonotonicSeconds() + durationSec;
        s_suppressedLootAreasByBot[bot->GetGUID().GetRawValue()].push_back(area);
    }

    bool LootAction::HasLootableTargets(Player* bot, const std::set<uint64_t>& ignoredGuids)
    {
        if (!bot || !bot->IsInWorld()) return false;

        PruneIgnoredLootGuids();
        PruneSuppressedLootAreas();

        FindLootCheck check(bot, 30.0f, ignoredGuids);
        std::list<Creature*> creatures;
        Trinity::CreatureListSearcher<FindLootCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, 30.0f);

        if (!creatures.empty()) return true;

        std::list<GameObject*> gameObjects;
        Trinity::GameObjectListSearcher<FindLootCheck> goSearcher(bot, gameObjects, check);
        Cell::VisitGridObjects(bot, goSearcher, 30.0f);

        return !gameObjects.empty();
    }

    bool LootAction::HasInventoryBlockedLoot(Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return false;

        PruneInventoryBlockedLootGuids();
        uint64_t botGuid = bot->GetGUID().GetRawValue();
        auto botIt = s_inventoryBlockedLootByBot.find(botGuid);
        if (botIt == s_inventoryBlockedLootByBot.end())
            return false;

        uint32_t nowSec = Helper::MonotonicSeconds();
        uint32_t freeSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);
        auto& targets = botIt->second;
        for (auto targetIt = targets.begin(); targetIt != targets.end(); )
        {
            targetIt = LootCapacityRetryPolicy::IsResolved(targetIt->second,
                nowSec, freeSlots) ? targets.erase(targetIt) : std::next(targetIt);
        }
        if (targets.empty())
        {
            s_inventoryBlockedLootByBot.erase(botIt);
            return false;
        }
        return true;
    }

    std::size_t LootAction::GetInventoryBlockedLootCount(Player* bot)
    {
        HasInventoryBlockedLoot(bot);
        if (!bot)
            return 0;
        auto botIt = s_inventoryBlockedLootByBot.find(bot->GetGUID().GetRawValue());
        return botIt == s_inventoryBlockedLootByBot.end() ? 0 : botIt->second.size();
    }

    ObjectGuid LootAction::FindLootTarget(Player* bot)
    {
        PruneIgnoredLootGuids();
        PruneSuppressedLootAreas();

        FindLootCheck check(bot, 30.0f);
        std::list<Creature*> creatures;
        Trinity::CreatureListSearcher<FindLootCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, 30.0f);

        if (!creatures.empty())
        {
            auto closest = std::min_element(creatures.begin(), creatures.end(), [bot](Creature* a, Creature* b) {
                return bot->GetDistance(a) < bot->GetDistance(b);
            });
            return (*closest)->GetGUID();
        }

        std::list<GameObject*> gameObjects;
        Trinity::GameObjectListSearcher<FindLootCheck> goSearcher(bot, gameObjects, check);
        Cell::VisitGridObjects(bot, goSearcher, 30.0f);

        if (!gameObjects.empty())
        {
            auto closest = std::min_element(gameObjects.begin(), gameObjects.end(), [bot](GameObject* a, GameObject* b) {
                return bot->GetDistance(a) < bot->GetDistance(b);
            });
            return (*closest)->GetGUID();
        }

        return ObjectGuid::Empty;
    }

    void LootAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        _channelStarted = false;
        _channelTimerMs = 0;
        _failsafe.Reset();
        _travelProgress.Reset();

        if (!bot || !bot->IsInWorld())
        {
            Finish(ActionOutcome::RetryableFailure, "bot was unavailable when looting started",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        if (!_targetGuid)
        {
            _targetGuid = FindLootTarget(bot);
        }

        if (!_targetGuid)
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }

        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, _targetGuid);
        if (!obj)
        {
            AddIgnoredLootGuid(bot, _targetGuid.GetRawValue());
            Finish(ActionOutcome::RetryableFailure, "loot target disappeared before movement started",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        if (movement)
        {
            uint64_t pathGeneration = movement->GetPathAttemptGeneration();
            bool moving = movement->MoveTo(obj->GetPositionX(), obj->GetPositionY(),
                obj->GetPositionZ(), BotMovementState::Moving, false);
            if (!moving && movement->GetPathAttemptGeneration() != pathGeneration)
            {
                AddIgnoredLootGuid(bot, _targetGuid.GetRawValue(), 300);
                SuppressDangerousArea(bot, _targetGuid, 25.0f, 300);
                Finish(ActionOutcome::RetryableFailure,
                    std::string("loot target path was rejected: ") +
                        movement->GetLastPathFailureName(),
                    FailureCategory::Navigation, RecoveryDirective::RetryLater);
            }
        }
    }

    void LootAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || _completed) return;

        bool isMoving = movement && movement->HasPath();
        if (_failsafe.CheckMovementProgress(deltaMs,
            Constants::LootFailsafeMovingTimeoutMs,
            Constants::LootFailsafeIdleTimeoutMs, 30000, isMoving,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            if (_targetGuid)
            {
                AddIgnoredLootGuid(bot, _targetGuid.GetRawValue(), 300);
                SuppressDangerousArea(bot, _targetGuid, 25.0f, 300);
            }
            Finish(ActionOutcome::RetryableFailure,
                std::string("loot target travel made no progress (movement=") +
                    (movement ? movement->GetStateName() : "Unavailable") +
                    ", path=" + (movement ? movement->GetLastPathFailureName() : "Unavailable") + ")",
                FailureCategory::Navigation, RecoveryDirective::RetryLater);
            return;
        }

        if (!_targetGuid)
        {
            _outcome = ActionOutcome::Succeeded;
            _completed = true;
            return;
        }

        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, _targetGuid);
        if (!obj)
        {
            AddIgnoredLootGuid(bot, _targetGuid.GetRawValue());
            _outcome = ActionOutcome::RetryableFailure;
            _failureCategory = FailureCategory::Transient;
            _recoveryDirective = RecoveryDirective::RetryLater;
            _outcomeReason = "loot target disappeared before interaction";
            _completed = true;
            return;
        }

        float dist = bot->GetDistance(obj);
        _travelProgress.Observe(
            { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() },
            { obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ() },
            deltaMs);
        if (dist > 3.0f && _travelProgress.GetNoProgressMs() >= 8000)
        {
            AddIgnoredLootGuid(bot, _targetGuid.GetRawValue(), 300);
            SuppressDangerousArea(bot, _targetGuid, 25.0f, 300);
            Finish(ActionOutcome::RetryableFailure,
                "loot target distance did not improve for 8 seconds",
                FailureCategory::Navigation, RecoveryDirective::RetryLater);
            return;
        }
        if (dist <= (INTERACTION_DISTANCE - 1.0f) || dist <= 3.0f)
        {
            if (movement)
            {
                movement->Stop();
            }

            // Channeling native WoW kneeling looting animation (1.5 seconds)
            if (!_channelStarted)
            {
                _channelStarted = true;
                _channelTimerMs = 1500;

                bot->SetFacingToObject(obj);
                bot->HandleEmoteCommand(static_cast<Emote>(228)); // EMOTE_STATE_LOOT (228): Kneeling looting animation
                return;
            }

            if (_channelTimerMs > deltaMs)
            {
                _channelTimerMs -= deltaMs;
                return;
            }

            bot->HandleEmoteCommand(static_cast<Emote>(0)); // Reset emote state to standing
            // Always end this action after one loot attempt. PerformLoot
            // classifies capacity failures separately and defers every other
            // rejected target, preventing an ineligible item from creating a
            // tight action-recreation loop.
            bool looted = PerformLoot(bot);
            _outcome = looted ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure;
            if (!looted)
            {
                _failureCategory = FailureCategory::InventoryCapacity;
                _recoveryDirective = RecoveryDirective::Replan;
                _outcomeReason = "one or more loot items could not be stored";
            }
            _completed = true;
        }
        else if (movement)
        {
            uint64_t pathGeneration = movement->GetPathAttemptGeneration();
            bool moving = movement->MoveTo(obj->GetPositionX(), obj->GetPositionY(),
                obj->GetPositionZ(), BotMovementState::Moving, false);
            if (!moving && movement->GetPathAttemptGeneration() != pathGeneration)
            {
                AddIgnoredLootGuid(bot, _targetGuid.GetRawValue(), 300);
                SuppressDangerousArea(bot, _targetGuid, 25.0f, 300);
                Finish(ActionOutcome::RetryableFailure,
                    std::string("loot target re-path was rejected: ") +
                        movement->GetLastPathFailureName(),
                    FailureCategory::Navigation, RecoveryDirective::RetryLater);
            }
        }
    }

    void LootAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot)
        {
            bot->HandleEmoteCommand(static_cast<Emote>(0));
        }
        if (movement)
        {
            movement->Stop();
        }
    }

    bool LootAction::PerformLoot(Player* bot)
    {
        // TrinityCore 3.3.5 has no LOOT_CHEST value. Its native CMSG_LOOT
        // handler deliberately uses LOOT_CORPSE for both creatures and
        // ordinary chest/quest GameObjects; fishing holes use their separate
        // unsupported-server loot type and are not selected by this action.
        bot->SendLoot(_targetGuid, LOOT_CORPSE);

        Loot* loot = nullptr;
        if (_targetGuid.IsGameObject())
        {
            if (GameObject* go = bot->GetMap()->GetGameObject(_targetGuid))
                loot = &go->loot;
        }
        else if (_targetGuid.IsCreatureOrVehicle())
        {
            if (Creature* creature = bot->GetMap()->GetCreature(_targetGuid))
                loot = &creature->loot;
        }
        else if (_targetGuid.IsCorpse())
        {
            if (Corpse* corpse = ObjectAccessor::GetCorpse(*bot, _targetGuid))
                loot = &corpse->loot;
        }

        struct LootedItemInfo
        {
            std::string name;
            uint32 count;
        };
        std::vector<LootedItemInfo> lootedItems;
        uint32 moneyLooted = 0;

        bool storageFailure = false;
        bool capacityFailure = false;
        bool deferTarget = false;
        if (loot && !loot->isLooted())
        {
            if (!_targetGuid.IsGameObject() && loot->gold > 0)
            {
                moneyLooted = loot->gold;
            }

            uint32 maxSlots = loot->GetMaxSlotInLootFor(bot);
            for (uint32 slot = 0; slot < maxSlots; ++slot)
            {
                // Quest, free-for-all, and conditionally visible loot tracks
                // consumption in a per-player wrapper. The shared LootItem is
                // deliberately left unlooted for other eligible players, so
                // checking only LootItem::is_looted reports a false failure.
                NotNormalLootItem* questItem = nullptr;
                NotNormalLootItem* freeForAllItem = nullptr;
                NotNormalLootItem* conditionalItem = nullptr;
                LootItem* item = loot->LootItemInSlot(slot, bot,
                    &questItem, &freeForAllItem, &conditionalItem);
                if (!item || item->is_looted || item->itemid == 0)
                    continue;

                // StoreLootItem performs these eligibility checks before it
                // attempts inventory storage. In particular, looting one
                // corpse can complete a quest and make the same quest item on
                // the next corpse ineligible even though CanStoreNewItem
                // still reports ample bag capacity. Treat that as handled for
                // this bot, not as a retryable storage failure.
                bool eligibleForBot = item->AllowedForPlayer(bot) &&
                    (questItem || !item->is_blocked) &&
                    (item->rollWinnerGUID.IsEmpty() || item->rollWinnerGUID == bot->GetGUID());
                if (!eligibleForBot)
                {
                    if (Diagnostics::BotTrace::ShouldLog(bot))
                    {
                        ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(item->itemid);
                        TC_LOG_INFO("server", "[WorldBots] [Loot] Bot '{}' skipped item {} ('{}') x{} from loot GUID {} because it is no longer eligible; target will not be retried",
                            bot->GetName(), item->itemid,
                            itemProto ? itemProto->Name1 : "Unknown Item", item->count,
                            static_cast<unsigned long long>(_targetGuid.GetRawValue()));
                    }
                    continue;
                }

                ItemPosCountVec dest;
                InventoryResult storeResult = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item->itemid, item->count);
                if (storeResult != EQUIP_ERR_OK)
                {
                    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(item->itemid);
                    if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Loot] Bot '{}' could not store item {} ('{}') x{} from loot GUID {} (InventoryResult: {}); deferring this loot target",
                            bot->GetName(), item->itemid, itemProto ? itemProto->Name1 : "Unknown Item", item->count,
                            static_cast<unsigned long long>(_targetGuid.GetRawValue()), static_cast<uint32_t>(storeResult));
                    }
                    storageFailure = true;
                    if (storeResult == EQUIP_ERR_INVENTORY_FULL)
                        capacityFailure = true;
                    else
                        deferTarget = true;
                    continue;
                }

                uint32 itemId = item->itemid;
                uint32 itemCount = item->count;
                ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemId);
                bot->StoreLootItem(slot, loot);

                bool consumedForBot = item->is_looted ||
                    (questItem && questItem->is_looted) ||
                    (freeForAllItem && freeForAllItem->is_looted) ||
                    (conditionalItem && conditionalItem->is_looted);
                if (consumedForBot)
                {
                    lootedItems.push_back({ itemProto ? itemProto->Name1 : "Unknown Item", itemCount });
                }
                else
                {
                    if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Loot] Bot '{}' failed to store item {} ('{}') x{} from loot GUID {} despite preflight; deferring this loot target",
                            bot->GetName(), itemId, itemProto ? itemProto->Name1 : "Unknown Item", itemCount,
                            static_cast<unsigned long long>(_targetGuid.GetRawValue()));
                    }
                    storageFailure = true;
                    deferTarget = true;
                }
            }

            if (!_targetGuid.IsGameObject() && loot->gold > 0)
            {
                bot->ModifyMoney(loot->gold);
                loot->NotifyMoneyRemoved();
                loot->gold = 0;
            }
        }

        if (bot->GetVictim())
        {
            bot->AttackStop();
        }

        if (bot->GetSession())
        {
            bot->GetSession()->DoLootRelease(_targetGuid);
        }

        if (!storageFailure || deferTarget)
            AddIgnoredLootGuid(bot, _targetGuid.GetRawValue());

        std::string lootSummary;
        if (moneyLooted > 0)
        {
            uint32 g = moneyLooted / 10000;
            uint32 s = (moneyLooted % 10000) / 100;
            uint32 c = moneyLooted % 100;
            std::string moneyStr;
            if (g > 0) moneyStr += std::to_string(g) + "g ";
            if (s > 0 || g > 0) moneyStr += std::to_string(s) + "s ";
            moneyStr += std::to_string(c) + "c";

            lootSummary += moneyStr;
        }

        for (const auto& info : lootedItems)
        {
            if (!lootSummary.empty())
                lootSummary += ", ";
            if (info.count > 1)
                lootSummary += std::to_string(info.count) + "x " + info.name;
            else
                lootSummary += info.name;
        }

        if (lootSummary.empty())
        {
            lootSummary = "None";
        }

        uint32_t freeBagSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);

        if (capacityFailure)
            AddInventoryBlockedLootGuid(bot, _targetGuid.GetRawValue());
        else if (!storageFailure)
            RemoveInventoryBlockedLootGuid(bot, _targetGuid.GetRawValue());

        if (Diagnostics::BotTrace::ShouldLog(bot))
            TC_LOG_INFO("server", "[WorldBots] [Loot] Bot '{}' {} looting target GUID {} (Loot: {}) [Available Bag Slots: {}]",
                bot->GetName(), !storageFailure ? "completed" :
                    (capacityFailure ? "partially completed; capacity retry deferred" :
                        "partially completed; target deferred"),
                static_cast<unsigned long long>(_targetGuid.GetRawValue()), lootSummary, freeBagSlots);

        return !storageFailure;
    }

    void LootAction::ClearBotState(ObjectGuid botGuid)
    {
        if (!botGuid)
            return;
        uint64_t key = botGuid.GetRawValue();
        s_ignoredLootExpiryByBot.erase(key);
        s_inventoryBlockedLootByBot.erase(key);
        s_suppressedLootAreasByBot.erase(key);
    }

    void LootAction::ClearAllState()
    {
        s_ignoredLootExpiryByBot.clear();
        s_inventoryBlockedLootByBot.clear();
        s_suppressedLootAreasByBot.clear();
    }
}
