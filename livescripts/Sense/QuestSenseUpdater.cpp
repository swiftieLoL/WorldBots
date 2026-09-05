#include "Globals/ObjectMgr.h"
#include "SenseUpdaters.h"
#include "QuestTargetResolver.h"
#include "QuestItemSourceResolver.h"
#include "Helper/InventoryUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/QuestUtils.h"
#include "Helper/SpellUtils.h"
#include "Helper/QuestItemUsePolicy.h"
#include "Helper/ProgressionUtils.h"
#include "Helper/ProgressionPolicy.h"
#include "Config/BotConfig.h"
#include "DataStores/DBCStores.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "GameObject.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include "Log.h"
#include "Helper/NpcFinder.h"
#include "Helper/Constants.h"
#include "Helper/TimeUtils.h"
#include "Cache/BotCache.h"
#include "Brain/QuestAvailabilityPolicy.h"
#include "Brain/QuestProgressionPolicy.h"
#include "Brain/QuestExclusionPolicy.h"
#include "Travel/WorldTravel.h"
#include "Diagnostics/BotTrace.h"
#include <cstdio>
#include <algorithm>
#include <limits>

namespace Sense
{
    namespace
    {
        enum class SourceItemInteractionResolution
        {
            NotApplicable,
            Resolved,
            Unsupported
        };

        SourceItemInteractionResolution ResolveSourceItemInteraction(
            Player* bot, Quest const* questTemplate,
            uint32_t questId, uint64_t botDistributionKey,
            Blackboard::QuestObjectiveData& objective)
        {
            if (!bot || !questTemplate)
                return SourceItemInteractionResolution::NotApplicable;

            uint32_t sourceItemId = questTemplate->GetSrcItemId();
            ItemTemplate const* sourceTemplate = sourceItemId
                ? sObjectMgr->GetItemTemplate(sourceItemId) : nullptr;
            if (!sourceTemplate)
                return SourceItemInteractionResolution::NotApplicable;

            std::array<Helper::QuestItemUseSpellSlot,
                MAX_ITEM_PROTO_SPELLS> useSpellSlots{};
            for (uint8_t spellIndex = 0;
                spellIndex < MAX_ITEM_PROTO_SPELLS; ++spellIndex)
            {
                const auto& itemSpell = sourceTemplate->Spells[spellIndex];
                uint32_t authoredSpellId = itemSpell.SpellId > 0
                    ? static_cast<uint32_t>(itemSpell.SpellId) : 0;
                useSpellSlots[spellIndex] = {
                    authoredSpellId,
                    itemSpell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE,
                    authoredSpellId != 0 &&
                        sSpellMgr->GetSpellInfo(authoredSpellId) != nullptr
                };
            }
            uint32_t firstOnUseSpellId =
                Helper::SelectFirstValidQuestItemUseSpell(useSpellSlots);

            struct ResolvedTarget
            {
                Cache::SpellInteractionTarget target;
                uint32_t spellId = 0;
                Blackboard::PositionInfo position;
                float score = std::numeric_limits<float>::max();
                bool hasLocation = false;
            };

            ResolvedTarget best;
            bool hasConditionedUseTarget = false;
            for (uint8_t spellIndex = 0; spellIndex < MAX_ITEM_PROTO_SPELLS; ++spellIndex)
            {
                const auto& itemSpell = sourceTemplate->Spells[spellIndex];
                if (itemSpell.SpellId <= 0 ||
                    itemSpell.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                {
                    continue;
                }

                for (const Cache::SpellInteractionTarget& target :
                    Cache::BotCache::GetSpellInteractionTargets(itemSpell.SpellId))
                {
                    Blackboard::QuestTargetKind kind = target.isGameObject
                        ? Blackboard::QuestTargetKind::GameObject
                        : Blackboard::QuestTargetKind::Creature;
                    if (kind != objective.targetKind)
                        continue;

                    hasConditionedUseTarget = true;
                    if (!Helper::IsSupportedQuestItemUse(
                        sourceTemplate->ScriptId, firstOnUseSpellId,
                        static_cast<uint32_t>(itemSpell.SpellId)))
                    {
                        continue;
                    }

                    float x = 0.0f, y = 0.0f, z = 0.0f;
                    uint32_t mapId = 0;
                    bool hasLocation = Helper::FindDiversifiedLocationCascading(
                        target.entry, target.isGameObject, botDistributionKey,
                        questId, bot, x, y, z, mapId);

                    // Prefer an exact authored entry, then a target with a real
                    // spawn, then the nearest alternative. This remains fully
                    // data-driven when the credited entry is synthetic.
                    float score = hasLocation
                        ? 0.0f : std::numeric_limits<float>::max() / 2.0f;
                    if (target.entry != objective.targetEntry)
                        score += 1000.0f;
                    if (hasLocation)
                    {
                        score += Helper::DistanceSq2D(x, y,
                            bot->GetPositionX(), bot->GetPositionY());
                        if (mapId != bot->GetMapId())
                            score += std::numeric_limits<float>::max() / 4.0f;
                    }

                    if (score >= best.score)
                        continue;
                    best.target = target;
                    best.spellId = static_cast<uint32_t>(itemSpell.SpellId);
                    best.position = { x, y, z, mapId };
                    best.score = score;
                    best.hasLocation = hasLocation;
                }
            }

            if (best.spellId == 0)
            {
                return hasConditionedUseTarget
                    ? SourceItemInteractionResolution::Unsupported
                    : SourceItemInteractionResolution::NotApplicable;
            }

            objective.interactionEntry = best.target.entry;
            objective.interactionKind = best.target.isGameObject
                ? Blackboard::QuestTargetKind::GameObject
                : Blackboard::QuestTargetKind::Creature;
            objective.sourceItemId = sourceItemId;
            objective.sourceSpellId = best.spellId;
            objective.sourceSpellTargetsEntity = best.target.targetsEntity;
            objective.type = best.target.isGameObject
                ? Blackboard::QuestObjectiveType::InteractGameObject
                : Blackboard::QuestObjectiveType::CastOnCreature;
            if (best.hasLocation)
            {
                objective.location = best.position;
                objective.hasLocation = true;
            }

            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' procedurally resolved quest {} credit Entry {} to source Item {}, Spell {}, interaction {} Entry {}{}",
                    bot->GetName(), questId, objective.targetEntry,
                    objective.sourceItemId, objective.sourceSpellId,
                    best.target.isGameObject ? "GameObject" : "Creature",
                    objective.interactionEntry,
                    best.hasLocation ? " with a spawn location" : " without a spawn location");
            }
            return SourceItemInteractionResolution::Resolved;
        }

        bool IsQuestDestinationExcluded(const Blackboard::QuestState& quest,
            const Blackboard::PositionInfo& position, uint32_t nowSec)
        {
            return std::any_of(quest.excludedQuestDestinations.begin(),
                quest.excludedQuestDestinations.end(),
                [&](const Brain::DestinationSuppression& destination) {
                    return destination.Contains(position.mapId, position.x,
                        position.y, nowSec);
                });
        }
    }

    bool QuestSenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        return Detail::ServiceSubstate(bb.quest, deltaMs,
            [&]() { Refresh(bot, bb.quest); });
    }

    void QuestSenseUpdater::Refresh(Player* bot, Blackboard::QuestState& quest)
    {
        quest.availableQuests.clear();
        quest.activeQuests.clear();
        quest.completedQuests.clear();
        if (!bot || !bot->IsInWorld())
            return;

        EvaluateQuestLog(bot, quest);
        ScanNearbyQuestGivers(bot, quest);
        LocateWorldStarter(bot, quest);
    }

    void QuestSenseUpdater::EvaluateQuestLog(Player* bot, Blackboard::QuestState& quest)
    {
        quest.fullRescanTimerMs += quest.refreshIntervalMs;
        quest.worldStarterScanElapsedMs = std::min<uint32_t>(
            quest.worldStarterScanElapsedMs + quest.refreshIntervalMs,
            Blackboard::QuestState::WorldStarterRescanIntervalMs);
        bool forceFullRescan = (quest.fullRescanTimerMs >= Blackboard::QuestState::FullRescanIntervalMs);
        if (forceFullRescan)
            quest.fullRescanTimerMs = 0;

        if (!bot || !bot->IsInWorld()) return;

        const auto& statusMap = bot->getQuestStatusMap();

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId) continue;

            QuestStatus status = bot->GetQuestStatus(questId);
            Quest const* qTemplate = sObjectMgr->GetQuestTemplate(questId);
            if (qTemplate && Brain::IsExcludedQuest(questId,
                qTemplate->GetZoneOrSort()))
            {
                continue;
            }

            if (status == QUEST_STATUS_COMPLETE)
            {
                Blackboard::ReadyToTurnInQuest turnIn;
                turnIn.questId = questId;

                turnIn.hasTurnInPosition = QuestTargetResolver::ResolveNearestQuestEnder(bot, questId, turnIn.questGiverEntry,
                    turnIn.questGiverKind, turnIn.turnInPosition);

                quest.completedQuests.push_back(turnIn);
            }
            else if (status == QUEST_STATUS_INCOMPLETE)
            {
                Blackboard::ActiveQuest active;
                active.questId = questId;

                uint32 primaryObjectiveEntry = 0;
                Blackboard::QuestTargetKind primaryObjectiveKind = Blackboard::QuestTargetKind::None;

                if (qTemplate)
                {
                    uint64_t botDistributionKey =
                        static_cast<uint64_t>(bot->GetGUID().GetCounter());
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
                                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                                {
                                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' has incomplete exploration quest {} ('{}') but no area trigger or quest POI target could be resolved; turn-in fallback is disabled",
                                        bot->GetName(), questId, qTemplate->GetTitle());
                                }
                            }
                            else
                            {
                                active.hasTargetPosition = true;
                                active.requiresTravel = true;
                            }
                        }
                    }

                    constexpr uint32 MaxCreatureOrGoObjectives = 4;
                    uint32 creatureOrGoCount = std::min<uint32>(
                        qTemplate->GetReqCreatureOrGOcount(), MaxCreatureOrGoObjectives);
                    for (uint32 i = 0; i < creatureOrGoCount; ++i)
                    {
                        int32 reqEntry = qTemplate->RequiredNpcOrGo[i];
                        uint32 reqCount = qTemplate->RequiredNpcOrGoCount[i];
                        if (reqEntry && reqCount)
                        {
                            Blackboard::QuestObjectiveData objData;
                            objData.targetEntry = (reqEntry > 0) ? uint32(reqEntry) : uint32(-reqEntry);
                            objData.targetKind = reqEntry > 0 ? Blackboard::QuestTargetKind::Creature : Blackboard::QuestTargetKind::GameObject;
                            objData.interactionEntry = objData.targetEntry;
                            objData.interactionKind = objData.targetKind;
                            if (reqEntry > 0)
                            {
                                CreatureTemplate const* objectiveCreature =
                                    sObjectMgr->GetCreatureTemplate(objData.targetEntry);
                                FactionTemplateEntry const* botFaction =
                                    bot->GetFactionTemplateEntry();
                                FactionTemplateEntry const* objectiveFaction =
                                    objectiveCreature
                                        ? sFactionTemplateStore.LookupEntry(objectiveCreature->faction)
                                        : nullptr;
                                bool isHostileObjective = botFaction && objectiveFaction &&
                                    (objectiveFaction->IsHostileTo(*botFaction) ||
                                     botFaction->IsHostileTo(*objectiveFaction));
                                objData.combatLevelSuitable = !isHostileObjective ||
                                    Helper::IsQuestObjectiveCreatureSuitable(
                                        bot->GetLevel(), objectiveCreature->maxlevel,
                                        Config::BotConfig::GetQuestMaxLevelsAboveBot());
                            }
                            objData.requiredCount = reqCount;
                            if (itQ != statusMap.end())
                            {
                                objData.currentCount = itQ->second.CreatureOrGOCount[i];
                            }
                            SourceItemInteractionResolution sourceResolution =
                                ResolveSourceItemInteraction(bot, qTemplate,
                                    questId, botDistributionKey, objData);
                            if (sourceResolution ==
                                SourceItemInteractionResolution::Unsupported)
                            {
                                objData.type = Blackboard::QuestObjectiveType::Unsupported;
                                active.hasUnsupportedObjective = true;
                                if (Diagnostics::BotTrace::ShouldLog(
                                    bot, Diagnostics::LogEvent::Normal))
                                {
                                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot safely automate source Item {} for quest {} ('{}'): the conditioned spell is scripted or is not the item's primary ON_USE spell",
                                        bot->GetName(), qTemplate->GetSrcItemId(),
                                        questId, qTemplate->GetTitle());
                                }
                            }
                            else if (sourceResolution ==
                                SourceItemInteractionResolution::NotApplicable)
                            {
                                if (reqEntry < 0)
                                    objData.type = Blackboard::QuestObjectiveType::InteractGameObject;
                                else if (Cache::BotCache::IsCastCreditQuest(questId))
                                {
                                    objData.type = Blackboard::QuestObjectiveType::Unsupported;
                                    active.hasUnsupportedObjective = true;
                                    if (Diagnostics::BotTrace::ShouldLog(
                                        bot, Diagnostics::LogEvent::Normal))
                                    {
                                        TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot resolve the authored cast-credit interaction for quest {} ('{}'); marking it unsupported instead of retrying an empty spell",
                                            bot->GetName(), questId,
                                            qTemplate->GetTitle());
                                    }
                                }
                                else
                                    objData.type = Blackboard::QuestObjectiveType::TalkToCreature;

                                if (objData.type !=
                                    Blackboard::QuestObjectiveType::Unsupported)
                                {
                                    float objectiveX = 0.0f, objectiveY = 0.0f, objectiveZ = 0.0f;
                                    uint32 objectiveMap = 0;
                                    bool isGameObject = objData.interactionKind == Blackboard::QuestTargetKind::GameObject;
                                    if (Helper::FindDiversifiedLocationCascading(objData.interactionEntry, isGameObject,
                                        botDistributionKey, questId, bot, objectiveX, objectiveY, objectiveZ, objectiveMap))
                                    {
                                        objData.location = { objectiveX, objectiveY, objectiveZ, objectiveMap };
                                        objData.hasLocation = true;
                                    }
                                }
                            }
                            active.objectives.push_back(objData);
                        }
                    }

                    constexpr uint32 MaxItemObjectives = 6;
                    uint32 itemsCount = std::min<uint32>(
                        qTemplate->GetReqItemsCount(), MaxItemObjectives);
                    bool questExcluded = quest.excludedQuestIds.contains(questId);
                    for (uint32 i = 0; i < itemsCount; ++i)
                    {
                        uint32 reqItem = qTemplate->RequiredItemId[i];
                        uint32 reqCount = qTemplate->RequiredItemCount[i];
                        if (reqItem && reqCount)
                        {
                            active.objectives.push_back(QuestItemSourceResolver::Resolve(
                                bot, qTemplate, quest, questId, reqItem, reqCount,
                                forceFullRescan && !questExcluded));
                        }
                    }

                    // The Emblazoned Runeblade is a two-stage item objective:
                    // loot a Battle-worn Sword, then use it beside a runeforge.
                    // The final item is created by spell 51769 and therefore
                    // has no direct loot source for the generic resolver.
                    if (questId == 12619 && bot->GetMapId() == 609 &&
                        bot->GetClass() == CLASS_DEATH_KNIGHT &&
                        bot->GetItemCount(38631, false) == 0)
                    {
                        uint32_t routeEntry = bot->GetItemCount(38607, false) > 0
                            ? 28481u : 190584u;
                        bool routeIsGameObject = routeEntry == 190584;
                        for (Blackboard::QuestObjectiveData& objective : active.objectives)
                        {
                            if (objective.itemId != 38631 ||
                                objective.currentCount >= objective.requiredCount)
                            {
                                continue;
                            }

                            objective.targetEntry = routeEntry;
                            objective.targetKind = routeIsGameObject
                                ? Blackboard::QuestTargetKind::GameObject
                                : Blackboard::QuestTargetKind::Creature;
                            float x = 0.0f, y = 0.0f, z = 0.0f;
                            uint32_t mapId = 0;
                            if (Helper::FindDiversifiedLocationCascading(routeEntry,
                                routeIsGameObject, botDistributionKey, questId,
                                bot, x, y, z, mapId))
                            {
                                objective.location = { x, y, z, mapId };
                                objective.hasLocation = true;
                            }
                        }
                    }
                }

                if (!active.requiresExploration)
                {
                    uint32 fallbackEntry = 0;
                    Blackboard::QuestTargetKind fallbackKind = Blackboard::QuestTargetKind::None;
                    const Blackboard::QuestObjectiveData* bestObjective = nullptr;
                    float bestObjectiveDistanceSq = std::numeric_limits<float>::max();
                    for (const Blackboard::QuestObjectiveData& objective : active.objectives)
                    {
                        if (objective.currentCount >= objective.requiredCount)
                            continue;
                        if (objective.type ==
                            Blackboard::QuestObjectiveType::Unsupported)
                            continue;
                        if (!objective.combatLevelSuitable)
                            continue;

                        uint32_t routeEntry = objective.interactionEntry != 0
                            ? objective.interactionEntry : objective.targetEntry;
                        Blackboard::QuestTargetKind routeKind =
                            objective.interactionKind != Blackboard::QuestTargetKind::None
                            ? objective.interactionKind : objective.targetKind;
                        if (fallbackEntry == 0 && routeEntry != 0)
                        {
                            fallbackEntry = routeEntry;
                            fallbackKind = routeKind;
                        }

                        if (!objective.hasLocation)
                            continue;

                        float distanceSq = Helper::DistanceSq2D(objective.location.x, objective.location.y,
                            bot->GetPositionX(), bot->GetPositionY());
                        if (objective.location.mapId != bot->GetMapId())
                            distanceSq += std::numeric_limits<float>::max() / 4.0f;
                        if (!bestObjective || distanceSq < bestObjectiveDistanceSq)
                        {
                            bestObjective = &objective;
                            bestObjectiveDistanceSq = distanceSq;
                        }
                    }

                    if (bestObjective)
                    {
                        primaryObjectiveEntry = bestObjective->interactionEntry != 0
                            ? bestObjective->interactionEntry : bestObjective->targetEntry;
                        primaryObjectiveKind = bestObjective->interactionKind != Blackboard::QuestTargetKind::None
                            ? bestObjective->interactionKind : bestObjective->targetKind;
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

                uint32 targetEntryToFind = primaryObjectiveEntry;

                if (targetEntryToFind == 0 && active.objectives.empty() && !active.requiresExploration)
                {
                    active.isTalkOrTravelOnly = true;
                    Blackboard::PositionInfo enderPosition;
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
                    bool isGameObject = (primaryObjectiveKind == Blackboard::QuestTargetKind::GameObject);
                    if (Helper::FindDiversifiedLocationCascading(targetEntryToFind, isGameObject,
                        static_cast<uint64_t>(bot->GetGUID().GetCounter()), questId, bot, tx, ty, tz, tMap))
                    {
                        active.targetPosition = { tx, ty, tz, tMap };
                        active.hasTargetPosition = true;
                        active.requiresTravel = true;
                    }
                }

                quest.activeQuests.push_back(active);
            }
        }
    }

    void QuestSenseUpdater::ScanNearbyQuestGivers(Player* bot, Blackboard::QuestState& quest)
    {
        auto upsertLiveTurnIn = [&](Blackboard::ReadyToTurnInQuest turnIn) {
            auto existing = std::find_if(quest.completedQuests.begin(), quest.completedQuests.end(),
                [questId = turnIn.questId](const Blackboard::ReadyToTurnInQuest& candidate) {
                    return candidate.questId == questId;
                });
            if (existing != quest.completedQuests.end())
                *existing = std::move(turnIn);
            else
                quest.completedQuests.push_back(std::move(turnIn));
        };

        std::list<Creature*> creatures;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, Constants::QuestScanRadius);
        Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, Constants::QuestScanRadius);

        for (Creature* creature : creatures)
        {
            if (!creature || !creature->IsAlive() || !creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                continue;

            uint32 dialogStatus = bot->GetQuestDialogStatus(creature);

            Blackboard::PositionInfo creaturePosition{ creature->GetPositionX(),
                creature->GetPositionY(), creature->GetPositionZ(),
                creature->GetMapId() };
            if ((dialogStatus == DIALOG_STATUS_AVAILABLE ||
                 dialogStatus == DIALOG_STATUS_AVAILABLE_REP) &&
                !IsQuestDestinationExcluded(quest, creaturePosition,
                    Helper::MonotonicSeconds()))
            {
                const auto& startedQuests = Cache::BotCache::GetQuestStarters(creature->GetEntry());
                for (uint32 qId : startedQuests)
                {
                    if (Quest const* qTemplate = sObjectMgr->GetQuestTemplate(qId))
                    {
                        if (!Brain::IsExcludedQuest(qId, qTemplate->GetZoneOrSort()) &&
                            quest.excludedQuestIds.find(qId) == quest.excludedQuestIds.end() &&
                            bot->CanTakeQuest(qTemplate, false) &&
                            bot->CanAddQuest(qTemplate, false) &&
                            Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate))
                        {
                            bool alreadyKnown = std::any_of(quest.availableQuests.begin(), quest.availableQuests.end(),
                                [qId](const Blackboard::KnownQuest& kq) { return kq.questId == qId; });
                            if (!alreadyKnown)
                            {
                                Blackboard::KnownQuest known;
                                known.questId = qId;
                                known.questGiverGuid = creature->GetGUID();
                                known.questGiverEntry = creature->GetEntry();
                                known.questGiverPosition = creaturePosition;
                                known.questGiverKind = Blackboard::QuestTargetKind::Creature;
                                known.hasQuestGiverPosition = true;
                                quest.availableQuests.push_back(known);
                            }
                        }
                    }
                }
            }

            if (dialogStatus == DIALOG_STATUS_REWARD || dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD_REP)
            {
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                {
                    uint32 questId = bot->GetQuestSlotQuestId(slot);
                    if (!questId) continue;

                    const auto& questEnders = Cache::BotCache::GetQuestEnders(questId);
                    if (std::find(questEnders.begin(), questEnders.end(), creature->GetEntry()) == questEnders.end())
                        continue;

                    if (Quest const* qTemplate = sObjectMgr->GetQuestTemplate(questId))
                    {
                        if (Brain::IsExcludedQuest(questId,
                            qTemplate->GetZoneOrSort()))
                        {
                            continue;
                        }
                        uint32 rewardIndex = 0;
                        if (Helper::QuestUtils::SelectRewardWithAvailableSpace(bot, qTemplate, rewardIndex))
                        {
                            Blackboard::ReadyToTurnInQuest turnIn;
                            turnIn.questId = questId;
                            turnIn.questGiverGuid = creature->GetGUID();
                            turnIn.questGiverEntry = creature->GetEntry();
                            turnIn.turnInPosition = { creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetMapId() };
                            turnIn.questGiverKind = Blackboard::QuestTargetKind::Creature;
                            turnIn.hasTurnInPosition = true;

                            auto itA = std::remove_if(quest.activeQuests.begin(), quest.activeQuests.end(),
                                [questId](const Blackboard::ActiveQuest& a) { return a.questId == questId; });
                            quest.activeQuests.erase(itA, quest.activeQuests.end());
                            upsertLiveTurnIn(std::move(turnIn));
                        }
                    }
                }
            }
        }

        std::list<GameObject*> questObjects;
        Trinity::AllGameObjectsWithEntryInRange goCheck(bot, 0, Constants::QuestScanRadius);
        Trinity::GameObjectListSearcher<Trinity::AllGameObjectsWithEntryInRange> goSearcher(bot, questObjects, goCheck);
        Cell::VisitGridObjects(bot, goSearcher, Constants::QuestScanRadius);
        for (GameObject* go : questObjects)
        {
            if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_QUESTGIVER)
                continue;

            uint32 dialogStatus = bot->GetQuestDialogStatus(go);
            Blackboard::PositionInfo gameObjectPosition{ go->GetPositionX(),
                go->GetPositionY(), go->GetPositionZ(), go->GetMapId() };
            if ((dialogStatus == DIALOG_STATUS_AVAILABLE ||
                 dialogStatus == DIALOG_STATUS_AVAILABLE_REP) &&
                !IsQuestDestinationExcluded(quest, gameObjectPosition,
                    Helper::MonotonicSeconds()))
            {
                for (uint32 qId : Cache::BotCache::GetGameObjectQuestStarters(go->GetEntry()))
                {
                    Quest const* qTemplate = sObjectMgr->GetQuestTemplate(qId);
                    if (!qTemplate || Brain::IsExcludedQuest(qId, qTemplate->GetZoneOrSort()) ||
                        quest.excludedQuestIds.find(qId) != quest.excludedQuestIds.end() ||
                        !bot->CanTakeQuest(qTemplate, false) || !bot->CanAddQuest(qTemplate, false) ||
                        !Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate))
                        continue;
                    bool alreadyKnown = std::any_of(quest.availableQuests.begin(), quest.availableQuests.end(),
                        [qId](const Blackboard::KnownQuest& known) { return known.questId == qId; });
                    if (!alreadyKnown)
                    {
                        Blackboard::KnownQuest known;
                        known.questId = qId;
                        known.questGiverGuid = go->GetGUID();
                        known.questGiverEntry = go->GetEntry();
                        known.questGiverPosition = gameObjectPosition;
                        known.questGiverKind = Blackboard::QuestTargetKind::GameObject;
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
                    if (!qId)
                        continue;
                    Quest const* qTemplate = qId ? sObjectMgr->GetQuestTemplate(qId) : nullptr;
                    const auto& questEnders = Cache::BotCache::GetGameObjectQuestEnders(qId);
                    if (std::find(questEnders.begin(), questEnders.end(), go->GetEntry()) == questEnders.end())
                        continue;
                    if (qTemplate && Brain::IsExcludedQuest(qId,
                        qTemplate->GetZoneOrSort()))
                    {
                        continue;
                    }
                    uint32 rewardIndex = 0;
                    if (!qTemplate || !Helper::QuestUtils::SelectRewardWithAvailableSpace(bot, qTemplate, rewardIndex))
                        continue;
                    Blackboard::ReadyToTurnInQuest turnIn;
                    turnIn.questId = qId;
                    turnIn.questGiverGuid = go->GetGUID();
                    turnIn.questGiverEntry = go->GetEntry();
                    turnIn.turnInPosition = { go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), go->GetMapId() };
                    turnIn.questGiverKind = Blackboard::QuestTargetKind::GameObject;
                    turnIn.hasTurnInPosition = true;

                    auto itA = std::remove_if(quest.activeQuests.begin(), quest.activeQuests.end(),
                        [qId](const Blackboard::ActiveQuest& active) { return active.questId == qId; });
                    quest.activeQuests.erase(itA, quest.activeQuests.end());
                    upsertLiveTurnIn(std::move(turnIn));
                }
            }
        }
    }

    void QuestSenseUpdater::LocateWorldStarter(Player* bot, Blackboard::QuestState& quest)
    {
        // Keep working healthy local quests. If every active quest is deferred
        // or unsupported, discover unrelated quest work instead of forcing the
        // bot into Grind until its level changes.
        auto isSuitableQuest = [bot](uint32_t questId) {
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
            return questTemplate &&
                !Brain::IsExcludedQuest(questId,
                    questTemplate->GetZoneOrSort()) &&
                Helper::IsQuestGroupStructureSuitable(bot->GetGroup() != nullptr,
                    questTemplate->GetType(), questTemplate->GetSuggestedPlayers()) &&
                Helper::IsQuestLevelSuitable(bot->GetLevel(),
                    questTemplate->GetQuestLevel(),
                    Config::BotConfig::GetQuestMaxLevelsAboveBot());
        };
        bool hasActionableActiveQuest = Brain::HasActionableActiveQuest(
            quest.activeQuests, quest.excludedQuestIds,
            [&isSuitableQuest](const Blackboard::ActiveQuest& active) {
                return isSuitableQuest(active.questId);
            });
        bool hasSuitableNearbyQuest = std::any_of(
            quest.availableQuests.begin(), quest.availableQuests.end(),
            [&isSuitableQuest, &quest](const Blackboard::KnownQuest& available) {
                return isSuitableQuest(available.questId) &&
                    (!available.hasQuestGiverPosition ||
                     !IsQuestDestinationExcluded(quest,
                        available.questGiverPosition,
                        Helper::MonotonicSeconds()));
            });
        if (!Brain::ShouldDiscoverWorldQuestStarter(
            hasActionableActiveQuest, hasSuitableNearbyQuest))
            return;

        auto cachedStarterIsEligible = [&]() {
            if (!quest.hasCachedWorldStarter)
                return false;
            if (quest.excludedQuestIds.find(quest.cachedWorldStarter.questId) !=
                quest.excludedQuestIds.end())
                return false;
            if (quest.cachedWorldStarter.hasQuestGiverPosition &&
                IsQuestDestinationExcluded(quest,
                    quest.cachedWorldStarter.questGiverPosition,
                    Helper::MonotonicSeconds()))
                return false;
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(
                quest.cachedWorldStarter.questId);
            return questTemplate &&
                !Brain::IsExcludedQuest(quest.cachedWorldStarter.questId,
                    questTemplate->GetZoneOrSort()) &&
                bot->GetQuestStatus(quest.cachedWorldStarter.questId) == QUEST_STATUS_NONE &&
                Helper::IsQuestGroupStructureSuitable(bot->GetGroup() != nullptr,
                    questTemplate->GetType(), questTemplate->GetSuggestedPlayers()) &&
                Helper::IsQuestLevelSuitable(bot->GetLevel(), questTemplate->GetQuestLevel(),
                    Config::BotConfig::GetQuestMaxLevelsAboveBot()) &&
                bot->CanTakeQuest(questTemplate, false) &&
                bot->CanAddQuest(questTemplate, false) &&
                Helper::QuestUtils::CanReceiveQuestSourceItem(bot, questTemplate);
        };

        bool cachedEligible = cachedStarterIsEligible();
        bool cachedStarterInvalidated = quest.hasCachedWorldStarter && !cachedEligible;
        bool rescanDue = quest.availableQuests.empty() &&
            (!quest.worldStarterScanAttempted || cachedStarterInvalidated ||
             quest.worldStarterScanElapsedMs >= Blackboard::QuestState::WorldStarterRescanIntervalMs);
        if (rescanDue)
        {
            Blackboard::PositionInfo position;
            uint32_t questId = 0;
            uint32_t entry = 0;
            bool isGameObject = false;
            auto destinationFilter = [&quest](const Cache::PositionInfo& candidate) {
                return !IsQuestDestinationExcluded(quest, candidate,
                    Helper::MonotonicSeconds());
            };
            bool found = Cache::BotCache::FindNearestAvailableQuestStarter(bot,
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                bot->GetMapId(), position, questId, entry, isGameObject,
                quest.excludedQuestIds, destinationFilter);
            if (!found)
                found = Cache::BotCache::FindNearestAvailableQuestStarter(bot,
                    bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                    std::numeric_limits<uint32_t>::max(), position, questId, entry,
                    isGameObject, quest.excludedQuestIds, destinationFilter);

            // Global discovery can find a perfectly eligible quest on a
            // continent this bot cannot currently reach. Do not publish a
            // task that WorldTravel is guaranteed to reject; the bot can
            // grind or work local quests until its travel capabilities
            // change and the periodic scan tries again.
            if (found && position.mapId != bot->GetMapId() &&
                !Travel::WorldTravel::CanReach(bot, position))
            {
                found = false;
            }

            quest.worldStarterScanAttempted = true;
            quest.worldStarterScanElapsedMs = 0;
            quest.hasCachedWorldStarter = found;
            if (found)
            {
                quest.cachedWorldStarter = {};
                quest.cachedWorldStarter.questId = questId;
                quest.cachedWorldStarter.questGiverEntry = entry;
                quest.cachedWorldStarter.questGiverPosition = position;
                quest.cachedWorldStarter.questGiverKind = isGameObject
                    ? Blackboard::QuestTargetKind::GameObject : Blackboard::QuestTargetKind::Creature;
                quest.cachedWorldStarter.hasQuestGiverPosition = true;
                quest.cachedWorldStarter.isGlobalDiscovery = true;
            }
            cachedEligible = cachedStarterIsEligible();
        }

        // Nearby candidates and the cached world starter are complementary.
        // Publishing both keeps a selected long-distance quest stable while
        // the bot moves through the scan radius of unrelated quest givers.
        Brain::PublishCachedQuestCandidate(quest.availableQuests,
            quest.cachedWorldStarter, cachedEligible);
    }
}
