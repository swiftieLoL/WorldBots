#include "LootAction.h"
#include "Globals/ObjectAccessor.h"
#include "Globals/ObjectMgr.h"
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
#include <list>
#include <cstdio>
#include <unordered_map>
#include <ctime>

namespace Actions
{
    static std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> s_ignoredLootExpiryByBot;
    static std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> s_inventoryBlockedLootExpiryByBot;
    static const std::set<uint64_t> s_noIgnoredLootGuids;

    static void PruneIgnoredLootGuids()
    {
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
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

    static void AddIgnoredLootGuid(Player* bot, uint64_t rawGuid, uint32_t durationSec = 180)
    {
        if (!bot)
            return;
        uint32_t expirySec = static_cast<uint32_t>(time(nullptr)) + durationSec;
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
            if (targetIt != botIt->second.end() && static_cast<uint32_t>(time(nullptr)) < targetIt->second)
                return true;
        }
        return false;
    }

    static void PruneInventoryBlockedLootGuids()
    {
        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        for (auto botIt = s_inventoryBlockedLootExpiryByBot.begin();
             botIt != s_inventoryBlockedLootExpiryByBot.end(); )
        {
            auto& targets = botIt->second;
            for (auto targetIt = targets.begin(); targetIt != targets.end(); )
                targetIt = nowSec >= targetIt->second ? targets.erase(targetIt) : std::next(targetIt);

            botIt = targets.empty() ? s_inventoryBlockedLootExpiryByBot.erase(botIt) : std::next(botIt);
        }
    }

    static void AddInventoryBlockedLootGuid(Player* bot, uint64_t rawGuid)
    {
        if (!bot || rawGuid == 0)
            return;
        constexpr uint32_t BlockedLootRetrySeconds = 180;
        s_inventoryBlockedLootExpiryByBot[bot->GetGUID().GetRawValue()][rawGuid] =
            static_cast<uint32_t>(time(nullptr)) + BlockedLootRetrySeconds;
    }

    static void RemoveInventoryBlockedLootGuid(Player* bot, uint64_t rawGuid)
    {
        if (!bot)
            return;
        auto botIt = s_inventoryBlockedLootExpiryByBot.find(bot->GetGUID().GetRawValue());
        if (botIt == s_inventoryBlockedLootExpiryByBot.end())
            return;
        botIt->second.erase(rawGuid);
        if (botIt->second.empty())
            s_inventoryBlockedLootExpiryByBot.erase(botIt);
    }

    static bool IsInventoryBlockedLootGuid(Player* bot, uint64_t rawGuid)
    {
        if (!bot)
            return false;
        auto botIt = s_inventoryBlockedLootExpiryByBot.find(bot->GetGUID().GetRawValue());
        return botIt != s_inventoryBlockedLootExpiryByBot.end() &&
            botIt->second.find(rawGuid) != botIt->second.end();
    }

    struct FindLootCheck
    {
        Player* const i_bot;
        float i_range;
        const std::set<uint64_t>& i_ignored;

        FindLootCheck(Player* bot, float range, const std::set<uint64_t>& ignored)
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
        : _targetGuid(corpseGuid), _started(false), _completed(false)
    {
    }

    bool LootAction::HasLootableTargets(Player* bot, const std::set<uint64_t>& ignoredGuids)
    {
        if (!bot || !bot->IsInWorld()) return false;

        PruneIgnoredLootGuids();

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

        uint64_t botGuid = bot->GetGUID().GetRawValue();
        if (Helper::InventoryUtils::CountFreeBagSlots(bot) > 0)
        {
            s_inventoryBlockedLootExpiryByBot.erase(botGuid);
            return false;
        }

        PruneInventoryBlockedLootGuids();
        auto botIt = s_inventoryBlockedLootExpiryByBot.find(botGuid);
        return botIt != s_inventoryBlockedLootExpiryByBot.end() && !botIt->second.empty();
    }

    std::size_t LootAction::GetInventoryBlockedLootCount(Player* bot)
    {
        HasInventoryBlockedLoot(bot);
        if (!bot)
            return 0;
        auto botIt = s_inventoryBlockedLootExpiryByBot.find(bot->GetGUID().GetRawValue());
        return botIt == s_inventoryBlockedLootExpiryByBot.end() ? 0 : botIt->second.size();
    }

    ObjectGuid LootAction::FindLootTarget(Player* bot)
    {
        PruneIgnoredLootGuids();

        FindLootCheck check(bot, 30.0f, s_noIgnoredLootGuids);
        std::list<Creature*> creatures;
        Trinity::CreatureListSearcher<FindLootCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, 30.0f);

        if (!creatures.empty())
        {
            creatures.sort([bot](Creature* a, Creature* b) {
                return bot->GetDistance(a) < bot->GetDistance(b);
            });
            return creatures.front()->GetGUID();
        }

        std::list<GameObject*> gameObjects;
        Trinity::GameObjectListSearcher<FindLootCheck> goSearcher(bot, gameObjects, check);
        Cell::VisitGridObjects(bot, goSearcher, 30.0f);

        if (!gameObjects.empty())
        {
            gameObjects.sort([bot](GameObject* a, GameObject* b) {
                return bot->GetDistance(a) < bot->GetDistance(b);
            });
            return gameObjects.front()->GetGUID();
        }

        return ObjectGuid::Empty;
    }

    void LootAction::Start(Player* bot, MovementManager* movement)
    {
        _started = false;
        _completed = false;
        _channelStarted = false;
        _channelTimerMs = 0;
        _failsafe.Reset();

        if (!bot || !bot->IsInWorld())
        {
            _completed = true;
            return;
        }

        if (!_targetGuid)
        {
            _targetGuid = FindLootTarget(bot);
        }

        if (!_targetGuid)
        {
            _completed = true;
            return;
        }

        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, _targetGuid);
        if (!obj)
        {
            AddIgnoredLootGuid(bot, _targetGuid.GetRawValue());
            _completed = true;
            return;
        }

        if (movement)
        {
            movement->MoveTo(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), BotMovementState::Moving, false);
        }

        _started = true;
    }

    void LootAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || _completed) return;

        bool isMoving = movement && movement->HasPath();
        if (_failsafe.Check(deltaMs, Constants::LootFailsafeMovingTimeoutMs, Constants::LootFailsafeIdleTimeoutMs, isMoving))
        {
            if (_targetGuid)
                AddIgnoredLootGuid(bot, _targetGuid.GetRawValue());
            _completed = true;
            return;
        }

        if (!_targetGuid)
        {
            _completed = true;
            return;
        }

        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, _targetGuid);
        if (!obj)
        {
            AddIgnoredLootGuid(bot, _targetGuid.GetRawValue());
            _completed = true;
            return;
        }

        float dist = bot->GetDistance(obj);
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
            // Always end this action after one loot attempt. On capacity
            // failure PerformLoot leaves the target unignored, so the brain
            // can select it again after vendoring or another inventory change.
            PerformLoot(bot);
            _completed = true;
        }
        else if (movement)
        {
            movement->MoveTo(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), BotMovementState::Moving, false);
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

    bool LootAction::IsComplete() const
    {
        return _completed;
    }

    bool LootAction::PerformLoot(Player* bot)
    {
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
        if (loot && !loot->isLooted())
        {
            if (!_targetGuid.IsGameObject() && loot->gold > 0)
            {
                moneyLooted = loot->gold;
            }

            uint32 maxSlots = loot->GetMaxSlotInLootFor(bot);
            for (uint32 slot = 0; slot < maxSlots; ++slot)
            {
                LootItem* item = loot->LootItemInSlot(slot, bot);
                if (!item || item->is_looted || item->itemid == 0)
                    continue;

                ItemPosCountVec dest;
                InventoryResult storeResult = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item->itemid, item->count);
                if (storeResult != EQUIP_ERR_OK)
                {
                    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(item->itemid);
                    TC_LOG_WARN("server", "[WorldBots] [Loot] Bot '{}' could not store item {} ('{}') x{} from loot GUID {} (InventoryResult: {}); leaving loot available for retry",
                        bot->GetName(), item->itemid, itemProto ? itemProto->Name1 : "Unknown Item", item->count,
                        static_cast<unsigned long long>(_targetGuid.GetRawValue()), static_cast<uint32_t>(storeResult));
                    storageFailure = true;
                    continue;
                }

                uint32 itemId = item->itemid;
                uint32 itemCount = item->count;
                ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemId);
                bot->StoreLootItem(slot, loot);

                if (item->is_looted)
                {
                    lootedItems.push_back({ itemProto ? itemProto->Name1 : "Unknown Item", itemCount });
                }
                else
                {
                    TC_LOG_WARN("server", "[WorldBots] [Loot] Bot '{}' failed to store item {} ('{}') x{} from loot GUID {} despite preflight; leaving loot available for retry",
                        bot->GetName(), itemId, itemProto ? itemProto->Name1 : "Unknown Item", itemCount,
                        static_cast<unsigned long long>(_targetGuid.GetRawValue()));
                    storageFailure = true;
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

        if (!storageFailure)
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

        if (storageFailure && freeBagSlots == 0)
            AddInventoryBlockedLootGuid(bot, _targetGuid.GetRawValue());
        else if (!storageFailure)
            RemoveInventoryBlockedLootGuid(bot, _targetGuid.GetRawValue());

        if (Diagnostics::BotTrace::ShouldLog(bot))
            TC_LOG_INFO("server", "[WorldBots] [Loot] Bot '{}' {} looting target GUID {} (Loot: {}) [Available Bag Slots: {}]",
                bot->GetName(), storageFailure ? "partially completed; retry pending" : "completed",
                static_cast<unsigned long long>(_targetGuid.GetRawValue()), lootSummary, freeBagSlots);

        return !storageFailure;
    }
}
