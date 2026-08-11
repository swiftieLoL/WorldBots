#include "Globals/ObjectMgr.h"
#include "BlackboardUpdater.h"
#include "QuestTargetResolver.h"
#include "QuestItemSourceResolver.h"
#include "Helper/InventoryUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/QuestUtils.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Bag.h"
#include "Creature.h"
#include "GameObject.h"
#include "QuestDef.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Helper/NpcFinder.h"
#include "Helper/Constants.h"
#include "Cache/BotCache.h"
#include <cstdio>
#include <algorithm>
#include <limits>

namespace Blackboard
{
    template <typename StateStruct, typename UpdateFn>
    static void ServiceSubstate(StateStruct& state, uint32_t deltaMs, UpdateFn&& updateFn)
    {
        state.elapsedMs += deltaMs;
        if (state.elapsedMs >= state.refreshIntervalMs)
        {
            state.elapsedMs -= state.refreshIntervalMs;
            updateFn();
        }
    }

    void BlackboardUpdater::UpdateAll(Player* bot, MovementManager* movement, BotBlackboard& bb, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld()) return;

        ServiceSubstate(bb.self, deltaMs, [&]() { UpdateSelf(bot, bb.self); });
        ServiceSubstate(bb.spatial, deltaMs, [&]() { UpdateSpatial(bot, bb.spatial); });
        ServiceSubstate(bb.party, deltaMs, [&]() { UpdateParty(bot, bb.party); });
        ServiceSubstate(bb.combat, deltaMs, [&]() { UpdateCombat(bot, bb.combat); });
        ServiceSubstate(bb.nav, deltaMs, [&]() { UpdateNavigation(bot, movement, bb.nav); });
        ServiceSubstate(bb.inv, deltaMs, [&]() { UpdateInventory(bot, bb.inv); });
        ServiceSubstate(bb.quest, deltaMs, [&]() { UpdateQuest(bot, bb.quest); });
    }

    void BlackboardUpdater::UpdateSelf(Player* bot, SelfState& self)
    {
        Powers powerType = bot->GetPowerType();
        self.health = bot->GetHealth();
        self.maxHealth = bot->GetMaxHealth();
        self.mana = bot->GetPower(powerType);
        self.maxMana = bot->GetMaxPower(powerType);

        self.healthPct = (self.maxHealth > 0) ? static_cast<uint8_t>((self.health * 100) / self.maxHealth) : 0;
        self.manaPct = (self.maxMana > 0) ? static_cast<uint8_t>((self.mana * 100) / self.maxMana) : 0;
        self.isLowHealth = (self.healthPct < 30);
        self.isLowMana = (self.manaPct < 20);

        self.level = bot->GetLevel();
        self.money = bot->GetMoney();
        self.x = bot->GetPositionX();
        self.y = bot->GetPositionY();
        self.z = bot->GetPositionZ();
        self.orientation = bot->GetOrientation();
        self.mapId = bot->GetMapId();
        self.areaId = bot->GetAreaId();
        self.zoneId = bot->GetZoneId();
        self.inCombat = bot->IsInCombat();
        self.isDead = !bot->IsAlive();
        self.isMounted = bot->IsMounted();
        self.isSwimming = bot->IsInWater() || bot->IsUnderWater();

        // Crowd-Control & Aura Perception
        self.isStunned = bot->HasUnitState(UNIT_STATE_STUNNED);
        self.isFeared = bot->HasUnitState(UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED);
        self.isSilenced = bot->HasAuraType(SPELL_AURA_MOD_SILENCE);
        self.isRooted = bot->HasUnitState(UNIT_STATE_ROOT);
        self.isCCed = self.isStunned || self.isFeared || self.isSilenced || self.isRooted;
    }

    void BlackboardUpdater::UpdateSpatial(Player* bot, SpatialState& spatial)
    {
        spatial.hostileGuids.clear();
        spatial.nearbyPlayerGuids.clear();
        spatial.nearbyCorpseGuids.clear();
        spatial.nearbyGameObjectGuids.clear();
        spatial.nearestEnemyGuid.Clear();
        spatial.nearestEnemyInLoSGuid.Clear();
        spatial.nearestFriendlyGuid.Clear();
        spatial.nearestNpcGuid.Clear();
        spatial.nearestCorpseGuid.Clear();
        spatial.nearestGameObjectGuid.Clear();

        if (bot->IsBeingTeleported())
        {
            return;
        }

        std::list<Unit*> units;
        Trinity::AnyUnitInObjectRangeCheck check(bot, Constants::TacticalScanRadius);
        Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
        Cell::VisitGridObjects(bot, searcher, Constants::TacticalScanRadius);

        float minEnemyDist = 99999.0f;
        float minEnemyLoSDist = 99999.0f;
        float minFriendDist = 99999.0f;
        float minNpcDist = 99999.0f;
        float minCorpseDist = 99999.0f;

        for (Unit* u : units)
        {
            if (!u || u == bot) continue;

            float dist = bot->GetDistance(u);

            if (u->IsAlive())
            {
                // Hostile & Neutral Target Filter: Include true hostiles, active attackers, and yellow neutral attackable mobs (excluding passive critters)
                bool isHostile = u->IsHostileTo(bot) || u->GetVictim() == bot || u->IsInCombatWith(bot);
                if (!isHostile && !bot->IsFriendlyTo(u))
                {
                    if (u->IsCreature() && !u->IsCritter() && u->isTargetableForAttack())
                    {
                        Creature* c = static_cast<Creature*>(u);
                        if (!c->IsCivilian())
                        {
                            isHostile = true;
                        }
                    }
                }

                if (isHostile)
                {
                    spatial.hostileGuids.push_back(u->GetGUID());
                    if (dist < minEnemyDist)
                    {
                        minEnemyDist = dist;
                        spatial.nearestEnemyGuid = u->GetGUID();
                    }

                    if (dist < minEnemyLoSDist && bot->IsWithinLOSInMap(u))
                    {
                        minEnemyLoSDist = dist;
                        spatial.nearestEnemyInLoSGuid = u->GetGUID();
                    }
                }
                else if (bot->IsFriendlyTo(u))
                {
                    if (dist < minFriendDist)
                    {
                        minFriendDist = dist;
                        spatial.nearestFriendlyGuid = u->GetGUID();
                    }
                }

                if (u->IsCreature() && dist < minNpcDist)
                {
                    minNpcDist = dist;
                    spatial.nearestNpcGuid = u->GetGUID();
                }

                if (u->GetTypeId() == TYPEID_PLAYER)
                {
                    spatial.nearbyPlayerGuids.push_back(u->GetGUID());
                }
            }
            else
            {
                // Dead Corpse Loot Sensing
                if (u->IsCreature() && u->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
                {
                    spatial.nearbyCorpseGuids.push_back(u->GetGUID());
                    if (dist < minCorpseDist)
                    {
                        minCorpseDist = dist;
                        spatial.nearestCorpseGuid = u->GetGUID();
                    }
                }
            }
        }

        // Interactive GameObject Sensing
        std::list<GameObject*> gameObjects;
        Trinity::AllGameObjectsWithEntryInRange goCheck(bot, 0, Constants::TacticalScanRadius);
        Trinity::GameObjectListSearcher<Trinity::AllGameObjectsWithEntryInRange> goSearcher(bot, gameObjects, goCheck);
        Cell::VisitGridObjects(bot, goSearcher, Constants::TacticalScanRadius);

        float minGoDist = 99999.0f;
        for (GameObject* go : gameObjects)
        {
            if (go && go->isSpawned())
            {
                float dist = bot->GetDistance(go);
                spatial.nearbyGameObjectGuids.push_back(go->GetGUID());
                if (dist < minGoDist)
                {
                    minGoDist = dist;
                    spatial.nearestGameObjectGuid = go->GetGUID();
                }
            }
        }
    }

    void BlackboardUpdater::UpdateParty(Player* bot, PartyState& party)
    {
        party.memberGuids.clear();
        party.groupLeaderGuid.Clear();
        party.lowestHealthGroupMemberGuid.Clear();
        party.groupTargetGuid.Clear();
        party.isInGroup = false;
        party.isGroupLeader = false;

        if (!bot || !bot->IsInWorld()) return;

        Group* group = bot->GetGroup();
        if (!group) return;

        party.isInGroup = true;
        party.groupLeaderGuid = group->GetLeaderGUID();
        party.isGroupLeader = (group->GetLeaderGUID() == bot->GetGUID());

        float lowestHealthPct = 1.0f;

        for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsInWorld())
            {
                party.memberGuids.push_back(member->GetGUID());

                if (member->IsAlive())
                {
                    float hpPct = (float)member->GetHealth() / (float)member->GetMaxHealth();
                    if (hpPct < lowestHealthPct)
                    {
                        lowestHealthPct = hpPct;
                        party.lowestHealthGroupMemberGuid = member->GetGUID();
                    }
                }
            }
        }

        if (Player* leader = ObjectAccessor::FindPlayer(party.groupLeaderGuid))
        {
            if (Unit* victim = leader->GetVictim())
            {
                if (victim->IsAlive())
                {
                    party.groupTargetGuid = victim->GetGUID();
                }
            }
        }
    }

    void BlackboardUpdater::UpdateCombat(Player* bot, CombatState& combat)
    {
        combat.attackerGuids.clear();
        combat.primaryAttackerGuid.Clear();

        if (Unit* victim = bot->GetVictim())
        {
            if (victim->IsAlive())
            {
                combat.currentTargetGuid = victim->GetGUID();
            }
            else
            {
                combat.currentTargetGuid.Clear();
            }
        }
        else
        {
            combat.currentTargetGuid.Clear();
        }

        auto attackersCopy = bot->getAttackers();
        float minAttackerDist = 99999.0f;

        for (Unit* attacker : attackersCopy)
        {
            if (attacker && attacker->IsAlive())
            {
                combat.attackerGuids.push_back(attacker->GetGUID());
                float dist = bot->GetDistance(attacker);
                if (dist < minAttackerDist)
                {
                    minAttackerDist = dist;
                    combat.primaryAttackerGuid = attacker->GetGUID();
                }
            }
        }

        combat.totalThreat = static_cast<uint32_t>(combat.attackerGuids.size());
        combat.spellReady = !bot->HasUnitState(UNIT_STATE_CASTING);
    }

    void BlackboardUpdater::UpdateNavigation(Player* bot, MovementManager* movement, NavigationState& nav)
    {
        if (movement)
        {
            nav.movementState = static_cast<uint8_t>(movement->GetState());
            nav.hasActivePath = movement->HasPath();
            nav.isStuck = (movement->GetState() == BotMovementState::Stuck);
            nav.destinationX = movement->GetDestinationX();
            nav.destinationY = movement->GetDestinationY();
            nav.destinationZ = movement->GetDestinationZ();
        }
    }

    void BlackboardUpdater::UpdateInventory(Player* bot, InventoryState& inv)
    {
        inv.freeBagSlots = 0;
        inv.totalBagSlots = 0;
        inv.hasItemsToSell = false;
        inv.needsRepair = false;
        inv.bagsFull = false;
        inv.hasHealthPotion = false;
        inv.hasManaPotion = false;
        inv.hasFood = false;
        inv.hasWater = false;
        inv.nearestVendorEntry = 0;
        inv.vendorPosition = {};

        if (!bot || !bot->IsInWorld()) return;

        inv.freeBagSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);
        Helper::InventoryPolicyContext inventoryPolicy =
            Helper::InventoryUtils::BuildPolicyContext(bot, inv.freeBagSlots <= 3);

        auto inspectItem = [&](Item* item) {
            if (!item) return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto) return;

            if (Helper::InventoryUtils::IsSellableForSpace(bot, item, inventoryPolicy))
                inv.hasItemsToSell = true;

            if (proto->Class == ITEM_CLASS_CONSUMABLE)
            {
                if (proto->SubClass == ITEM_SUBCLASS_POTION)
                {
                    for (const auto& spell : proto->Spells)
                    {
                        if (spell.SpellCategory == 4)
                            inv.hasHealthPotion = true;
                        else if (spell.SpellCategory == 5)
                            inv.hasManaPotion = true;
                    }
                }
                else if (proto->SubClass == ITEM_SUBCLASS_FOOD || proto->SubClass == 5)
                {
                    inv.hasFood = true;
                    inv.hasWater = true;
                }
                else if (proto->SubClass == ITEM_SUBCLASS_BANDAGE)
                {
                    inv.hasFood = true;
                }
            }
        };

        inv.totalBagSlots = (INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START);

        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Bag* bag = bot->GetBagByPos(i))
            {
                inv.totalBagSlots += bag->GetBagSize();
            }
        }

        Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 /*bag*/, uint8 /*slot*/, Item* item) -> bool {
            inspectItem(item);
            return true;
        });

        inv.bagsFull = (inv.freeBagSlots == 0);
        inv.lowBagSpace = (inv.freeBagSlots <= 3);

        // 3. Check Durability for Repair Needs & Calculate Durability Pct
        uint32 totalMaxDur = 0;
        uint32 totalCurDur = 0;
        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        {
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                uint32 maxDur = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
                if (maxDur > 0)
                {
                    uint32 curDur = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
                    totalMaxDur += maxDur;
                    totalCurDur += curDur;
                    if ((float)curDur / (float)maxDur < 0.8f)
                    {
                        inv.needsRepair = true;
                    }
                }
            }
        }
        inv.durabilityPct = (totalMaxDur > 0) ? static_cast<uint8_t>((totalCurDur * 100) / totalMaxDur) : 100;

        // 4. Query in-memory cache for nearest Vendor/Repair NPC
        if (inv.hasItemsToSell || inv.needsRepair || inv.bagsFull)
        {
            Cache::PositionInfo pos;
            uint32_t entry = 0;
            bool requireVendor = inv.hasItemsToSell || !inv.needsRepair;
            if (Cache::BotCache::FindNearestVendor(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                requireVendor, inv.needsRepair, entry, pos))
            {
                inv.nearestVendorEntry = entry;
                inv.vendorPosition.mapId = pos.mapId;
                inv.vendorPosition.x = pos.x;
                inv.vendorPosition.y = pos.y;
                inv.vendorPosition.z = pos.z;
            }
        }
    }

    void BlackboardUpdater::UpdateQuest(Player* bot, QuestState& quest)
    {
        quest.availableQuests.clear();
        quest.activeQuests.clear();
        quest.completedQuests.clear();

        // Track full rescan timer for item source cache
        quest.fullRescanTimerMs += quest.refreshIntervalMs;
        bool forceFullRescan = (quest.fullRescanTimerMs >= QuestState::FullRescanIntervalMs);
        if (forceFullRescan)
            quest.fullRescanTimerMs = 0;

        if (!bot || !bot->IsInWorld()) return;

        const auto& statusMap = bot->getQuestStatusMap();

        // 1. Process active and completed quests in Player QuestLog
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId) continue;

            QuestStatus status = bot->GetQuestStatus(questId);
            Quest const* qTemplate = sObjectMgr->GetQuestTemplate(questId);

            if (status == QUEST_STATUS_COMPLETE)
            {
                ReadyToTurnInQuest turnIn;
                turnIn.questId = questId;

                turnIn.hasTurnInPosition = QuestTargetResolver::ResolveNearestQuestEnder(bot, questId, turnIn.questGiverEntry,
                    turnIn.questGiverKind, turnIn.turnInPosition);

                quest.completedQuests.push_back(turnIn);
            }
            else if (status == QUEST_STATUS_INCOMPLETE)
            {
                ActiveQuest active;
                active.questId = questId;

                uint32 primaryObjectiveEntry = 0;
                QuestTargetKind primaryObjectiveKind = QuestTargetKind::None;

                if (qTemplate)
                {
                    active.isTalkOrTravelOnly = (qTemplate->GetReqCreatureOrGOcount() == 0 && qTemplate->GetReqItemsCount() == 0);

                    auto itQ = statusMap.find(questId);
                    if (qTemplate->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
                    {
                        active.requiresExploration = itQ == statusMap.end() || !itQ->second.Explored;
                        if (active.requiresExploration)
                        {
                            active.isTalkOrTravelOnly = false;
                            if (!QuestTargetResolver::ResolveExplorationTarget(bot, qTemplate, active.targetPosition, active.explorationRadius))
                            {
                                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' has incomplete exploration quest {} ('{}') but no area trigger or quest POI target could be resolved; turn-in fallback is disabled",
                                    bot->GetName(), questId, qTemplate->GetTitle());
                            }
                            else
                            {
                                active.hasTargetPosition = true;
                                active.requiresTravel = true;
                            }
                        }
                    }

                    // Parse Kill / Creature / GO Objectives
                    uint32 creatureOrGoCount = qTemplate->GetReqCreatureOrGOcount();
                    for (uint32 i = 0; i < creatureOrGoCount; ++i)
                    {
                        int32 reqEntry = qTemplate->RequiredNpcOrGo[i];
                        uint32 reqCount = qTemplate->RequiredNpcOrGoCount[i];
                        if (reqEntry && reqCount)
                        {
                            QuestObjectiveData objData;
                            objData.targetEntry = (reqEntry > 0) ? uint32(reqEntry) : uint32(-reqEntry);
                            objData.targetKind = reqEntry > 0 ? QuestTargetKind::Creature : QuestTargetKind::GameObject;
                            if (reqEntry < 0)
                                objData.type = QuestObjectiveType::InteractGameObject;
                            else if (Cache::BotCache::IsCastCreditQuest(questId))
                                objData.type = QuestObjectiveType::CastOnCreature;
                            else
                                // TrinityCore computes both KILL and SPEAKTO
                                // flags for every creature objective. The
                                // executor decides between combat and talk
                                // from the live creature's attackability.
                                objData.type = QuestObjectiveType::TalkToCreature;
                            objData.requiredCount = reqCount;
                            if (itQ != statusMap.end())
                            {
                                objData.currentCount = itQ->second.CreatureOrGOCount[i];
                            }
                            float objectiveX = 0.0f, objectiveY = 0.0f, objectiveZ = 0.0f;
                            uint32 objectiveMap = 0;
                            bool objectiveLocated = objData.targetKind == QuestTargetKind::GameObject
                                ? Helper::FindGameObjectLocation(objData.targetEntry, objectiveX, objectiveY, objectiveZ,
                                    objectiveMap, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId())
                                : Helper::FindNpcLocation(objData.targetEntry, objectiveX, objectiveY, objectiveZ,
                                    objectiveMap, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
                            if (objectiveLocated)
                            {
                                objData.location = { objectiveX, objectiveY, objectiveZ, objectiveMap };
                                objData.hasLocation = true;
                            }
                            active.objectives.push_back(objData);
                        }
                    }

                    // Parse item objectives through the dedicated source resolver.
                    uint32 itemsCount = qTemplate->GetReqItemsCount();
                    for (uint32 i = 0; i < itemsCount; ++i)
                    {
                        uint32 reqItem = qTemplate->RequiredItemId[i];
                        uint32 reqCount = qTemplate->RequiredItemCount[i];
                        if (reqItem && reqCount)
                        {
                            active.objectives.push_back(QuestItemSourceResolver::Resolve(
                                bot, qTemplate, quest, questId, reqItem, reqCount, forceFullRescan));
                        }
                    }
                }
                // Choose from objectives only after every incomplete entry has
                // been resolved. An unresolved first template objective must
                // not hide a later objective that has a reachable spawn.
                if (!active.requiresExploration)
                {
                    uint32 fallbackEntry = 0;
                    QuestTargetKind fallbackKind = QuestTargetKind::None;
                    const QuestObjectiveData* bestObjective = nullptr;
                    float bestObjectiveDistanceSq = std::numeric_limits<float>::max();
                    for (const QuestObjectiveData& objective : active.objectives)
                    {
                        if (objective.currentCount >= objective.requiredCount)
                            continue;

                        if (fallbackEntry == 0 && objective.targetEntry != 0)
                        {
                            fallbackEntry = objective.targetEntry;
                            fallbackKind = objective.targetKind;
                        }

                        if (!objective.hasLocation || objective.location.mapId != bot->GetMapId())
                            continue;

                        float distanceSq = Helper::DistanceSq2D(objective.location.x, objective.location.y,
                            bot->GetPositionX(), bot->GetPositionY());
                        if (!bestObjective || distanceSq < bestObjectiveDistanceSq)
                        {
                            bestObjective = &objective;
                            bestObjectiveDistanceSq = distanceSq;
                        }
                    }

                    if (bestObjective)
                    {
                        primaryObjectiveEntry = bestObjective->targetEntry;
                        primaryObjectiveKind = bestObjective->targetKind;
                        active.targetPosition = bestObjective->location;
                        active.hasTargetPosition = true;
                        active.requiresTravel = true;
                    }
                    else
                    {
                        primaryObjectiveEntry = fallbackEntry;
                        primaryObjectiveKind = fallbackKind;
                    }
                }

                // If incomplete objective exists, point active.targetPosition to mob or GameObject spawn area!
                uint32 targetEntryToFind = (primaryObjectiveEntry != 0) ? primaryObjectiveEntry : 0;

                // A genuinely empty objective list may represent a scripted
                // speak/delivery quest. Do not misclassify an unresolved item
                // or entity objective as a quest-ender interaction.
                if (targetEntryToFind == 0 && active.objectives.empty() && !active.requiresExploration)
                {
                    active.isTalkOrTravelOnly = true;
                    PositionInfo enderPosition;
                    if (QuestTargetResolver::ResolveNearestQuestEnder(bot, questId, targetEntryToFind,
                        primaryObjectiveKind, enderPosition))
                    {
                        active.targetNpcEntry = targetEntryToFind;
                        active.targetPosition = enderPosition;
                        active.hasTargetPosition = true;
                        active.requiresTravel = true;
                    }
                    else
                        active.hasUnsupportedObjective = true;
                }

                active.targetKind = primaryObjectiveKind;
                if (targetEntryToFind != 0 && !active.hasTargetPosition)
                {
                    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
                    uint32 tMap = 0;
                    bool foundTarget = primaryObjectiveKind == QuestTargetKind::GameObject
                        ? Helper::FindGameObjectLocation(targetEntryToFind, tx, ty, tz, tMap, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId())
                        : Helper::FindNpcLocation(targetEntryToFind, tx, ty, tz, tMap, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
                    if (foundTarget)
                    {
                        active.targetPosition = { tx, ty, tz, tMap };
                        active.hasTargetPosition = true;
                        active.requiresTravel = true;
                    }
                }

                quest.activeQuests.push_back(active);
            }
        }

        auto upsertLiveTurnIn = [&](ReadyToTurnInQuest turnIn) {
            auto existing = std::find_if(quest.completedQuests.begin(), quest.completedQuests.end(),
                [questId = turnIn.questId](const ReadyToTurnInQuest& candidate) {
                    return candidate.questId == questId;
                });
            if (existing != quest.completedQuests.end())
                *existing = std::move(turnIn);
            else
                quest.completedQuests.push_back(std::move(turnIn));
        };

        // 2. Scan nearby NPCs for available quests and rewardable quest turn-ins
        std::list<Creature*> creatures;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, Constants::QuestScanRadius);
        Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, Constants::QuestScanRadius);

        for (Creature* creature : creatures)
        {
            if (!creature || !creature->IsAlive() || !creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                continue;

            uint32 dialogStatus = bot->GetQuestDialogStatus(creature);

            // Available quest check
            if (dialogStatus == DIALOG_STATUS_AVAILABLE || dialogStatus == DIALOG_STATUS_AVAILABLE_REP)
            {
                auto startedQuests = Cache::BotCache::GetQuestStarters(creature->GetEntry());
                for (uint32 qId : startedQuests)
                {
                    if (Quest const* qTemplate = sObjectMgr->GetQuestTemplate(qId))
                    {
                        if (bot->CanTakeQuest(qTemplate, false) &&
                            bot->CanAddQuest(qTemplate, false) &&
                            Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate))
                        {
                            bool alreadyKnown = std::any_of(quest.availableQuests.begin(), quest.availableQuests.end(),
                                [qId](const KnownQuest& kq) { return kq.questId == qId; });
                            if (!alreadyKnown)
                            {
                                KnownQuest known;
                                known.questId = qId;
                                known.questGiverGuid = creature->GetGUID();
                                known.questGiverEntry = creature->GetEntry();
                                known.questGiverPosition = { creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetMapId() };
                                known.questGiverKind = QuestTargetKind::Creature;
                                known.hasQuestGiverPosition = true;
                                quest.availableQuests.push_back(known);
                            }
                        }
                    }
                }
            }

            // Completed / rewardable quest turn-in check (MUST PASS checkCompleteness = true)
            if (dialogStatus == DIALOG_STATUS_REWARD || dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD_REP)
            {
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                {
                    uint32 questId = bot->GetQuestSlotQuestId(slot);
                    if (!questId) continue;

                    auto questEnders = Cache::BotCache::GetQuestEnders(questId);
                    if (std::find(questEnders.begin(), questEnders.end(), creature->GetEntry()) == questEnders.end())
                        continue;

                    if (Quest const* qTemplate = sObjectMgr->GetQuestTemplate(questId))
                    {
                        uint32 rewardIndex = 0;
                        if (Helper::QuestUtils::SelectRewardWithAvailableSpace(bot, qTemplate, rewardIndex))
                        {
                            ReadyToTurnInQuest turnIn;
                            turnIn.questId = questId;
                            turnIn.questGiverGuid = creature->GetGUID();
                            turnIn.questGiverEntry = creature->GetEntry();
                            turnIn.turnInPosition = { creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetMapId() };
                            turnIn.questGiverKind = QuestTargetKind::Creature;
                            turnIn.hasTurnInPosition = true;

                            // Purge from activeQuests
                            auto itA = std::remove_if(quest.activeQuests.begin(), quest.activeQuests.end(),
                                [questId](const ActiveQuest& a) { return a.questId == questId; });
                            quest.activeQuests.erase(itA, quest.activeQuests.end());

                            // Prefer the exact live giver GUID over a static
                            // database position, especially for summoned or
                            // dynamically moved quest enders.
                            upsertLiveTurnIn(std::move(turnIn));
                        }
                    }
                }
            }
        }

        // GameObject quest starters and enders use the same dialog status and
        // quest eligibility rules as creatures, but live in separate relation
        // tables and entry namespaces.
        std::list<GameObject*> questObjects;
        Trinity::AllGameObjectsWithEntryInRange goCheck(bot, 0, Constants::QuestScanRadius);
        Trinity::GameObjectListSearcher<Trinity::AllGameObjectsWithEntryInRange> goSearcher(bot, questObjects, goCheck);
        Cell::VisitGridObjects(bot, goSearcher, Constants::QuestScanRadius);
        for (GameObject* go : questObjects)
        {
            if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_QUESTGIVER)
                continue;

            uint32 dialogStatus = bot->GetQuestDialogStatus(go);
            if (dialogStatus == DIALOG_STATUS_AVAILABLE || dialogStatus == DIALOG_STATUS_AVAILABLE_REP)
            {
                for (uint32 qId : Cache::BotCache::GetGameObjectQuestStarters(go->GetEntry()))
                {
                    Quest const* qTemplate = sObjectMgr->GetQuestTemplate(qId);
                    if (!qTemplate || !bot->CanTakeQuest(qTemplate, false) || !bot->CanAddQuest(qTemplate, false) ||
                        !Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate))
                        continue;
                    bool alreadyKnown = std::any_of(quest.availableQuests.begin(), quest.availableQuests.end(),
                        [qId](const KnownQuest& known) { return known.questId == qId; });
                    if (!alreadyKnown)
                    {
                        KnownQuest known;
                        known.questId = qId;
                        known.questGiverGuid = go->GetGUID();
                        known.questGiverEntry = go->GetEntry();
                        known.questGiverPosition = { go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), go->GetMapId() };
                        known.questGiverKind = QuestTargetKind::GameObject;
                        known.hasQuestGiverPosition = true;
                        quest.availableQuests.push_back(known);
                    }
                }
            }

            if (dialogStatus == DIALOG_STATUS_REWARD || dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD_REP)
            {
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                {
                    uint32 qId = bot->GetQuestSlotQuestId(slot);
                    Quest const* qTemplate = qId ? sObjectMgr->GetQuestTemplate(qId) : nullptr;
                    auto questEnders = qId ? Cache::BotCache::GetGameObjectQuestEnders(qId) : std::vector<uint32_t>{};
                    if (!qId || std::find(questEnders.begin(), questEnders.end(), go->GetEntry()) == questEnders.end())
                        continue;
                    uint32 rewardIndex = 0;
                    if (!qTemplate || !Helper::QuestUtils::SelectRewardWithAvailableSpace(bot, qTemplate, rewardIndex))
                        continue;
                    ReadyToTurnInQuest turnIn;
                    turnIn.questId = qId;
                    turnIn.questGiverGuid = go->GetGUID();
                    turnIn.questGiverEntry = go->GetEntry();
                    turnIn.turnInPosition = { go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), go->GetMapId() };
                    turnIn.questGiverKind = QuestTargetKind::GameObject;
                    turnIn.hasTurnInPosition = true;

                    auto itA = std::remove_if(quest.activeQuests.begin(), quest.activeQuests.end(),
                        [qId](const ActiveQuest& active) { return active.questId == qId; });
                    quest.activeQuests.erase(itA, quest.activeQuests.end());
                    upsertLiveTurnIn(std::move(turnIn));
                }
            }
        }
    }
}
