#include "QuestAction.h"
#include "Globals/ObjectMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Helper/NpcFinder.h"
#include "Helper/Constants.h"
#include "Helper/QuestUtils.h"
#include "Cache/BotCache.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include <algorithm>

namespace Actions
{
    void TurnInQuestAction::Start(Player* /*bot*/, MovementManager* /*movement*/)
    {
        _completed = false;
        _outcome = ActionOutcome::Running;
        _outcomeReason.clear();
        _failsafe.Reset();
        _retryLogTimerMs = 5000;
    }

    void TurnInQuestAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !movement)
        {
            _outcome = ActionOutcome::RetryableFailure;
            _outcomeReason = "bot or movement manager was unavailable during quest turn-in";
            _completed = true;
            return;
        }

        if (_retryLogTimerMs < 5000)
            _retryLogTimerMs += deltaMs;

        if (blackboard.quest.completedQuests.empty())
        {
            _outcome = bot->GetQuestStatus(_selectedQuestId) == QUEST_STATUS_NONE
                ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure;
            if (_outcome != ActionOutcome::Succeeded)
                _outcomeReason = "completed quest disappeared from the blackboard before turn-in";
            _completed = true;
            return;
        }

        const Blackboard::ReadyToTurnInQuest* selected = nullptr;
        for (const auto& candidate : blackboard.quest.completedQuests)
        {
            if (candidate.questId == _selectedQuestId)
            {
                selected = &candidate;
                break;
            }
        }
        if (!selected)
        {
            _outcome = bot->GetQuestStatus(_selectedQuestId) == QUEST_STATUS_NONE
                ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure;
            if (_outcome != ActionOutcome::Succeeded)
                _outcomeReason = "selected quest disappeared from the completed quest list";
            _completed = true;
            return;
        }
        const auto& completed = *selected;

        QuestStatus liveStatus = bot->GetQuestStatus(completed.questId);
        if (liveStatus == QUEST_STATUS_NONE)
        {
            // A preceding town-run step already turned this quest in and the
            // one-second blackboard snapshot has not refreshed yet.
            _outcome = ActionOutcome::Succeeded;
            _completed = true;
            return;
        }
        if (liveStatus != QUEST_STATUS_COMPLETE)
        {
            _outcome = ActionOutcome::RetryableFailure;
            _outcomeReason = "quest is no longer in a rewardable completed state";
            _completed = true;
            return;
        }

        if (!completed.hasTurnInPosition || completed.turnInPosition.mapId != bot->GetMapId())
        {
            TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot turn in quest {} because its turn-in is on map {} while the bot is on map {}",
                bot->GetName(), completed.questId, completed.turnInPosition.mapId, bot->GetMapId());
            _outcome = ActionOutcome::Blocked;
            _outcomeReason = "quest ender is missing or requires cross-map travel";
            _completed = true;
            return;
        }
        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
        if (!questTemplate)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Quest] Cannot turn in unknown quest template {}", completed.questId);
            _outcome = ActionOutcome::Unsupported;
            _outcomeReason = "quest template is missing";
            _completed = true;
            return;
        }

        const auto questEnders = completed.questGiverKind == Blackboard::QuestTargetKind::GameObject
            ? Cache::BotCache::GetGameObjectQuestEnders(completed.questId)
            : Cache::BotCache::GetQuestEnders(completed.questId);
        if (std::find(questEnders.begin(), questEnders.end(), completed.questGiverEntry) == questEnders.end())
        {
            _outcome = ActionOutcome::Blocked;
            _outcomeReason = "selected object is not registered as an ender for this quest";
            _completed = true;
            return;
        }

        constexpr uint32_t TravelHardTimeoutMs = 10 * 60 * 1000;
        if (_failsafe.CheckMovementProgress(deltaMs, Constants::FailsafeMovingTimeoutMs,
            Constants::FailsafeIdleTimeoutMs, TravelHardTimeoutMs, movement->HasPath(),
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            _outcome = ActionOutcome::Blocked;
            _outcomeReason = "quest ender travel made no progress or exceeded the hard turn-in timeout";
            _completed = true;
            return;
        }

        if (!Helper::NpcUtils::IsInInteractionRange(bot, completed.turnInPosition.x, completed.turnInPosition.y, completed.turnInPosition.z, Constants::QuestInteractionRange))
        {
            movement->MoveTo(completed.turnInPosition.x, completed.turnInPosition.y, completed.turnInPosition.z, BotMovementState::Moving, false);
            return;
        }

        bool turnInSucceeded = false;
        bool retryThroughBrain = false;
        auto rewardFrom = [&](Object* questGiver) {
            uint32 rewardIndex = 0;
            if (!Helper::QuestUtils::SelectRewardWithAvailableSpace(bot, questTemplate, rewardIndex))
            {
                if (_retryLogTimerMs >= 5000)
                {
                    uint32 missingItemId = 0;
                    uint32 itemCount = 0;
                    uint32 requiredCount = 0;
                    if (Helper::QuestUtils::FindMissingRequiredItem(bot, questTemplate, missingItemId, itemCount, requiredCount))
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot turn in quest {} ('{}'): missing required item {}: {}/{} in inventory",
                            bot->GetName(), completed.questId, questTemplate->GetTitle(), missingItemId, itemCount, requiredCount);
                    }
                    else if (Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate))
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot turn in quest {} ('{}'): reward inventory space is unavailable; returning control to the brain for vendoring",
                            bot->GetName(), completed.questId, questTemplate->GetTitle());
                    }
                    else
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot turn in quest {} ('{}'): another reward prerequisite is unavailable",
                            bot->GetName(), completed.questId, questTemplate->GetTitle());
                    }
                    _retryLogTimerMs = 0;
                }

                // Do not vend, move, or retry from this action. Completing it
                // returns control to BotBrain, which owns the Vendor goal.
                retryThroughBrain = true;
                return;
            }

            bot->RewardQuest(questTemplate, rewardIndex, questGiver, true);
            turnInSucceeded = bot->GetQuestStatus(completed.questId) == QUEST_STATUS_NONE;

            if (turnInSucceeded)
            {
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' (GUID: {}) TURNED IN quest {} ('{}') to {} (Entry: {})",
                        bot->GetName(), bot->GetGUID().GetCounter(), completed.questId, questTemplate->GetTitle(),
                        completed.questGiverKind == Blackboard::QuestTargetKind::GameObject ? "GameObject" : "NPC",
                        completed.questGiverEntry);
                }
            }
            else
            {
                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' attempted to turn in quest {} ('{}'), but the quest remained active",
                    bot->GetName(), completed.questId, questTemplate->GetTitle());
            }
        };

        if (completed.questGiverKind == Blackboard::QuestTargetKind::GameObject)
        {
            GameObject* go = completed.questGiverGuid
                ? bot->GetMap()->GetGameObject(completed.questGiverGuid)
                : bot->FindNearestGameObject(completed.questGiverEntry, Constants::DefaultNpcSearchRadius);
            if (go)
            {
                Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(bot, go);
                if (status == Helper::InteractionStatus::NeedsMovement)
                {
                    movement->MoveTo(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(),
                        BotMovementState::Moving, false);
                    return;
                }
                if (status == Helper::InteractionStatus::Ready)
                {
                    go->Use(bot);
                    rewardFrom(go);
                }
            }
        }
        else
        {
            Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntry(bot,
                completed.questGiverEntry, Constants::DefaultNpcSearchRadius);
            if (creature && creature->IsAlive())
            {
                Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(bot, creature);
                if (status == Helper::InteractionStatus::NeedsMovement)
                {
                    movement->MoveTo(creature->GetPositionX(), creature->GetPositionY(),
                        creature->GetPositionZ(), BotMovementState::Moving, false);
                    return;
                }
                if (status == Helper::InteractionStatus::Ready)
                {
                    Helper::NpcUtils::PrepareCreatureInteraction(bot, creature);
                    rewardFrom(creature);
                }
            }
        }

        _completed = turnInSucceeded || retryThroughBrain;
        if (turnInSucceeded)
            _outcome = ActionOutcome::Succeeded;
        else if (retryThroughBrain)
        {
            _outcome = ActionOutcome::RetryableFailure;
            _outcomeReason = "quest reward prerequisites remained unavailable at the quest giver";
        }
    }

    void TurnInQuestAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
            movement->Stop();
    }
}
