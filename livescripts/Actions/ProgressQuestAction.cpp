#include "QuestAction.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Cache/BotCache.h"
#include "Helper/NpcFinder.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include <algorithm>
#include <cmath>
#include <list>
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

    ProgressQuestAction::ProgressQuestAction(uint32_t questId)
        : _completed(false), _lockedQuestId(questId), _lastProgressSubPath(SubPath_None),
          _searchExpandTimerMs(0), _searchExpandCount(0), _deliveryRetryLogTimerMs(5000)
    {
    }

    void ProgressQuestAction::Start(Player* bot, MovementManager* /*movement*/)
    {
        _completed = false;
        _outcome = ActionOutcome::Running;
        _outcomeReason.clear();
        _objectiveTargetGuid.Clear();
        _classStrategy.reset();
        _lastProgressSubPath = SubPath_None;
        _searchExpandTimerMs = 0;
        _searchExpandCount = 0;
        _deliveryRetryLogTimerMs = 5000;
        _sourceRecoveryCooldownMs = 0;
        _noProgressTimerMs = 0;
        _targetAcquireTimerMs = 0;
        _interactionRetryTimerMs = 0;
        _creatureSearchCooldownMs = 0;
        _gameObjectSearchCooldownMs = 0;
        _lastProgressSignature = 0;
        _worldTravel.Reset();

        if (!_lockedQuestId)
            Finish(ActionOutcome::Blocked, "the brain did not provide a quest context");
        else if (bot)
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
    }

    void ProgressQuestAction::Finish(ActionOutcome outcome, std::string reason)
    {
        _outcome = outcome;
        _outcomeReason = std::move(reason);
        _completed = true;
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

        _objectiveTargetGuid = target->GetGUID();
        bot->SetSelection(target->GetGUID());
        if (_lastProgressSubPath != SubPath_Combat)
        {
            _lastProgressSubPath = SubPath_Combat;
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' engaging '{}' (Entry: {}, GUID: {}) while progressing quest {}",
                    bot->GetName(), target->GetName(), target->GetEntry(), target->GetGUID().GetCounter(), _lockedQuestId);
        }
        _classStrategy->UpdateCombat(bot, target, movement, blackboard, deltaMs);
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
            if ((objective.type == Blackboard::QuestObjectiveType::KillCreature ||
                 objective.type == Blackboard::QuestObjectiveType::TalkToCreature) && objective.targetEntry)
                validEntries.insert(objective.targetEntry);
            else if (objective.type == Blackboard::QuestObjectiveType::CollectItem && objective.itemId)
            {
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
                if (target->IsAlive() && validEntries.find(target->GetEntry()) != validEntries.end())
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
            if (objective.type == Blackboard::QuestObjectiveType::InteractGameObject && objective.targetEntry)
                validEntries.insert(objective.targetEntry);
            else if (objective.type == Blackboard::QuestObjectiveType::CollectItem && objective.itemId)
            {
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
            movement->MoveTo(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), BotMovementState::Moving, false);
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
            target->Use(bot);
            _interactionRetryTimerMs = 2000;
            _lastProgressSubPath = SubPath_Interact;
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' used objective GameObject Entry {} for quest {}",
                    bot->GetName(), target->GetEntry(), _lockedQuestId);
        }
        return true;
    }

    void ProgressQuestAction::WanderObjectiveArea(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, const Blackboard::ActiveQuest& active,
        Quest const* qTemplate, float searchRadius, uint32_t deltaMs)
    {
        if (!active.hasTargetPosition)
        {
            movement->Stop();
            Finish(active.hasUnsupportedObjective ? ActionOutcome::Unsupported : ActionOutcome::Blocked,
                active.hasUnsupportedObjective ? "quest contains an unsupported objective" : "no objective location or spawn could be resolved");
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
            movement->MoveTo(active.targetPosition.x, active.targetPosition.y, active.targetPosition.z,
                BotMovementState::Moving, false);
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
            movement->MoveTo(targetX, targetY, targetZ, BotMovementState::Moving, false);
        }
    }

    void ProgressQuestAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (_completed)
            return;
        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "bot or movement context became unavailable");
            return;
        }

        if (_deliveryRetryLogTimerMs < 5000)
            _deliveryRetryLogTimerMs += deltaMs;
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
            Finish(ActionOutcome::Unsupported, "quest template is missing");
            return;
        }
        if (active->hasTargetPosition &&
            (_worldTravel.IsActive() || Travel::WorldTravel::NeedsTravel(bot, active->targetPosition)))
        {
            if (_lastProgressSubPath != SubPath_Travel)
                _lastProgressSubPath = SubPath_Travel;
            Travel::TravelResult travelResult = _worldTravel.Update(bot, movement,
                active->targetPosition, deltaMs);
            if (travelResult == Travel::TravelResult::Failed)
                Finish(ActionOutcome::Blocked, _worldTravel.GetFailureReason());
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
            Finish(ActionOutcome::Blocked, "no objective counter or inventory progress for 120 seconds");
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
            threatGuid = blackboard.combat.currentTargetGuid;
        if (threatGuid)
        {
            if (Unit* threat = ObjectAccessor::GetUnit(*bot, threatGuid))
            {
                if (threat->IsAlive() && HandleCombatTarget(bot, movement, blackboard, threat, deltaMs))
                    return;
            }
        }

        if (active->requiresExploration)
        {
            if (!active->hasTargetPosition)
            {
                Finish(ActionOutcome::Unsupported, "exploration quest has no AreaTrigger or POI target");
                return;
            }
            float radius = active->explorationRadius > 0.0f ? active->explorationRadius : 8.0f;
            float distanceSq = Helper::DistanceSq(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                active->targetPosition.x, active->targetPosition.y, active->targetPosition.z);
            if (distanceSq > radius * radius)
            {
                movement->MoveTo(active->targetPosition.x, active->targetPosition.y, active->targetPosition.z,
                    BotMovementState::Moving, false);
                return;
            }
            movement->Stop();
            bot->AreaExploredOrEventHappens(active->questId);
            if (bot->GetQuestStatus(active->questId) == QUEST_STATUS_COMPLETE)
                Finish(ActionOutcome::Succeeded);
            return;
        }

        if (active->isTalkOrTravelOnly)
        {
            if (!active->hasTargetPosition || active->targetNpcEntry == 0)
            {
                Finish(ActionOutcome::Unsupported, "scripted talk/delivery quest has no resolvable target");
                return;
            }
            if (!Helper::NpcUtils::IsInInteractionRange(bot, active->targetPosition.x,
                active->targetPosition.y, active->targetPosition.z, Constants::QuestInteractionRange))
            {
                movement->MoveTo(active->targetPosition.x, active->targetPosition.y, active->targetPosition.z,
                    BotMovementState::Moving, false);
                return;
            }

            if (_interactionRetryTimerMs == 0)
            {
                movement->Stop();
                if (active->targetKind == Blackboard::QuestTargetKind::GameObject)
                {
                    if (GameObject* go = bot->FindNearestGameObject(active->targetNpcEntry, Constants::DefaultNpcSearchRadius))
                    {
                        Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(bot, go);
                        if (status == Helper::InteractionStatus::NeedsMovement)
                        {
                            movement->MoveTo(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(),
                                BotMovementState::Moving, false);
                            return;
                        }
                        if (status == Helper::InteractionStatus::Ready)
                            go->Use(bot);
                    }
                }
                else if (Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntry(bot,
                    active->targetNpcEntry, Constants::DefaultNpcSearchRadius))
                {
                    Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(bot, creature);
                    if (status == Helper::InteractionStatus::NeedsMovement)
                    {
                        movement->MoveTo(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(),
                            BotMovementState::Moving, false);
                        return;
                    }
                    if (status == Helper::InteractionStatus::Ready)
                        Helper::NpcUtils::PrepareCreatureInteraction(bot, creature);
                }
                _interactionRetryTimerMs = 2000;
            }
            if (bot->CanCompleteQuest(active->questId))
                bot->CompleteQuest(active->questId);
            if (bot->GetQuestStatus(active->questId) == QUEST_STATUS_COMPLETE)
                Finish(ActionOutcome::Succeeded);
            else if (_targetAcquireTimerMs >= InteractionNoProgressTimeoutMs)
                Finish(ActionOutcome::Unsupported, "scripted talk/delivery interaction produced no quest progress");
            return;
        }

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
                Finish(ActionOutcome::Unsupported, "quest has no automatable objectives and core still reports it incomplete");
            return;
        }

        // Database-authored CAST objectives need their own credit path. The
        // 3.3.5 quest schema identifies the credit target but not a universal
        // spell to cast, so use TrinityCore's native creature-credit API after
        // reaching the target rather than incorrectly reporting a conversation.
        for (const auto& objective : active->objectives)
        {
            if (objective.currentCount >= objective.requiredCount ||
                objective.type != Blackboard::QuestObjectiveType::CastOnCreature)
                continue;

            if (Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntry(bot, objective.targetEntry, 30.0f))
            {
                Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(bot, creature);
                if (status == Helper::InteractionStatus::NeedsMovement)
                    movement->MoveTo(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), BotMovementState::Moving, false);
                else if (status == Helper::InteractionStatus::Ready && _interactionRetryTimerMs == 0)
                {
                    movement->Stop();
                    bot->SetSelection(creature->GetGUID());
                    bot->KilledMonsterCredit(creature->GetEntry(), creature->GetGUID());
                    _interactionRetryTimerMs = 2000;
                }
                return;
            }
        }

        // TrinityCore computes SPEAKTO for every creature objective. Only use
        // talk credit when the live target is not a valid combat target;
        // hostile targets fall through to the combat objective finder below.
        for (const auto& objective : active->objectives)
        {
            if (objective.currentCount >= objective.requiredCount ||
                objective.type != Blackboard::QuestObjectiveType::TalkToCreature)
                continue;

            if (Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntry(bot, objective.targetEntry, 30.0f))
            {
                if (bot->IsValidAttackTarget(creature))
                    continue;
                Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(bot, creature);
                if (status == Helper::InteractionStatus::NeedsMovement)
                    movement->MoveTo(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), BotMovementState::Moving, false);
                else if (status == Helper::InteractionStatus::Ready && _interactionRetryTimerMs == 0)
                {
                    movement->Stop();
                    Helper::NpcUtils::PrepareCreatureInteraction(bot, creature);
                    _interactionRetryTimerMs = 2000;
                }
                return;
            }
        }

        if (FindAndUseObjectiveGameObject(bot, movement, *active))
            return;

        float searchRadius = std::min(105.0f, 30.0f + (_searchExpandCount * 25.0f));
        if (FindAndEngageObjectiveMob(bot, movement, blackboard, *active, qTemplate, searchRadius, deltaMs))
            return;

        if (_targetAcquireTimerMs >= InteractionNoProgressTimeoutMs && !active->hasTargetPosition)
        {
            Finish(ActionOutcome::Blocked, "no live objective target was found for 30 seconds");
            return;
        }
        WanderObjectiveArea(bot, movement, blackboard, *active, qTemplate, searchRadius, deltaMs);
    }

    void ProgressQuestAction::Stop(Player* bot, MovementManager* movement)
    {
        _worldTravel.Stop(bot, movement);
    }
}
