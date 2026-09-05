#include "QuestAction.h"
#include "TurnInInteractionPolicy.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Helper/NpcFinder.h"
#include "Helper/Constants.h"
#include "Helper/QuestUtils.h"
#include "Cache/BotCache.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/NpcApproachHelper.h"
#include <algorithm>

namespace Actions
{
    namespace
    {
        bool IsQuestNoLongerActive(Player* bot, uint32_t questId)
        {
            if (!bot)
                return false;
            QuestStatus status = bot->GetQuestStatus(questId);
            return status == QUEST_STATUS_NONE || status == QUEST_STATUS_REWARDED;
        }
    }

    void TurnInQuestAction::Start(Player* /*bot*/, MovementManager* /*movement*/)
    {
        ResetOutcome();
        _failsafe.Reset();
        _retryLogTimerMs = 5000;
        _questGiverResolveElapsedMs = 0;
        _worldTravel.Reset();
    }

    void TurnInQuestAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "bot or movement manager was unavailable during quest turn-in",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        if (_retryLogTimerMs < 5000)
            _retryLogTimerMs += deltaMs;

        if (blackboard.quest.completedQuests.empty())
        {
            if (IsQuestNoLongerActive(bot, _selectedQuestId))
                _outcome = ActionOutcome::Succeeded;
            else
            {
                _outcome = ActionOutcome::Interrupted;
                _failureCategory = FailureCategory::Transient;
                _recoveryDirective = RecoveryDirective::None;
                _outcomeReason = "completed quest disappeared from the blackboard before turn-in";
            }
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
            if (IsQuestNoLongerActive(bot, _selectedQuestId))
                _outcome = ActionOutcome::Succeeded;
            else
            {
                _outcome = ActionOutcome::Interrupted;
                _failureCategory = FailureCategory::Transient;
                _recoveryDirective = RecoveryDirective::None;
                _outcomeReason = "selected quest disappeared from the completed quest list";
            }
            _completed = true;
            return;
        }
        const auto& completed = *selected;

        QuestStatus liveStatus = bot->GetQuestStatus(completed.questId);
        if (IsQuestNoLongerActive(bot, completed.questId))
        {
            // A preceding town-run step already turned this quest in and the
            // one-second blackboard snapshot has not refreshed yet. Ordinary
            // one-shot quests report REWARDED here; repeatable quests report
            // NONE, and both mean the turn-in succeeded.
            _outcome = ActionOutcome::Succeeded;
            _completed = true;
            return;
        }
        if (liveStatus != QUEST_STATUS_COMPLETE)
        {
            // A live-state mismatch is a stale decision, not a quest failure.
            // Replan after the next sense refresh without suppressing a quest
            // that may have just advanced or been abandoned externally.
            _outcome = ActionOutcome::Interrupted;
            _failureCategory = FailureCategory::Transient;
            _recoveryDirective = RecoveryDirective::None;
            _outcomeReason = "quest is no longer in a rewardable completed state";
            _completed = true;
            return;
        }

        if (!completed.hasTurnInPosition)
        {
            _outcome = ActionOutcome::Blocked;
            _failureCategory = FailureCategory::Navigation;
            _recoveryDirective = RecoveryDirective::RetryLater;
            _outcomeReason = "quest ender position is missing";
            _completed = true;
            return;
        }

        // Let the live quest-ender resolver own nearby handoffs. In particular,
        // quest 333 starts about 73 yards from its ender: using WorldTravel at
        // 20 yards prevented the already-loaded ender from ever being approached.
        const bool needsWorldTravel = Travel::WorldTravel::NeedsTravel(bot,
            completed.turnInPosition,
            TurnInInteractionPolicy::LiveQuestEnderResolveRange);
        if (_worldTravel.IsActive() && !needsWorldTravel)
            _worldTravel.Stop(bot, movement);

        if (needsWorldTravel)
        {
            Travel::TravelResult travelResult = _worldTravel.Update(bot, movement,
                completed.turnInPosition, deltaMs, _dangerAreas,
                blackboard.spatial.hostileGuids);
            if (travelResult == Travel::TravelResult::Failed)
            {
                _outcome = ActionOutcome::Blocked;
                _failureCategory = FailureCategory::Navigation;
                _recoveryDirective = RecoveryDirective::RetryLater;
                _outcomeReason = _worldTravel.GetFailureReason();
                _completed = true;
            }
            else if (travelResult == Travel::TravelResult::Arrived)
                _failsafe.Reset();
            return;
        }
        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(completed.questId);
        if (!questTemplate)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Quest] Cannot turn in unknown quest template {}", completed.questId);
            _outcome = ActionOutcome::Unsupported;
            _failureCategory = FailureCategory::ContentUnsupported;
            _recoveryDirective = RecoveryDirective::RetryLater;
            _outcomeReason = "quest template is missing";
            _completed = true;
            return;
        }

        const auto& questEnders = completed.questGiverKind == Blackboard::QuestTargetKind::GameObject
            ? Cache::BotCache::GetGameObjectQuestEnders(completed.questId)
            : Cache::BotCache::GetQuestEnders(completed.questId);
        if (std::find(questEnders.begin(), questEnders.end(), completed.questGiverEntry) == questEnders.end())
        {
            _outcome = ActionOutcome::Blocked;
            _failureCategory = FailureCategory::ContentUnsupported;
            _recoveryDirective = RecoveryDirective::RetryLater;
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
            _failureCategory = FailureCategory::Navigation;
            _recoveryDirective = RecoveryDirective::RetryLater;
            _outcomeReason = std::string("quest ender travel failed: ") +
                _failsafe.GetMovementFailureReason();
            _completed = true;
            return;
        }

        // WorldTravel only owns the journey into the quest-end area. From
        // here, approach the live ender directly: its cached spawn coordinate
        // can be offset, on another floor, or stale even when the NPC is loaded
        // and immediately reachable.
        bool turnInSucceeded = false;
        bool retryThroughBrain = false;
        bool inventoryCapacityBlocked = false;
        auto rewardFrom = [&](Object* questGiver) {
            uint32 rewardIndex = 0;
            if (!Helper::QuestUtils::SelectRewardWithAvailableSpace(bot, questTemplate, rewardIndex))
            {
                if (_retryLogTimerMs >= 5000)
                {
                    if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
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
                    }
                    _retryLogTimerMs = 0;
                }

                // Do not vend, move, or retry from this action. Completing it
                // returns control to BotBrain, which owns the Vendor goal.
                retryThroughBrain = true;
                inventoryCapacityBlocked = Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate);
                return;
            }

            bot->RewardQuest(questTemplate, rewardIndex, questGiver, true);
            // RewardQuest removes the active quest and records ordinary
            // one-shot quests as REWARDED. Repeatable quests normally return
            // to NONE. Both states mean the turn-in completed successfully.
            turnInSucceeded = IsQuestNoLongerActive(bot, completed.questId);

            if (turnInSucceeded)
            {
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' (GUID: {}) TURNED IN quest {} ('{}') to {} (Entry: {})",
                        bot->GetName(), bot->GetGUID().GetCounter(), completed.questId, questTemplate->GetTitle(),
                        completed.questGiverKind == Blackboard::QuestTargetKind::GameObject ? "GameObject" : "NPC",
                        completed.questGiverEntry);
                }
            }
            else
            {
                if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' attempted to turn in quest {} ('{}'), but the quest remained active",
                        bot->GetName(), completed.questId, questTemplate->GetTitle());
                }
                retryThroughBrain = true;
            }
        };

        if (completed.questGiverKind == Blackboard::QuestTargetKind::GameObject)
        {
            GameObject* go = nullptr;
            auto res = Helper::ApproachGameObject(bot, movement, completed.questGiverEntry,
                completed.questGiverGuid,
                TurnInInteractionPolicy::LiveQuestEnderResolveRange, go);
            if (res == Helper::ApproachResult::Approaching)
            {
                _questGiverResolveElapsedMs = 0;
                _outcomeReason.clear();
                return;
            }
            if (res == Helper::ApproachResult::Ready)
            {
                _questGiverResolveElapsedMs = 0;
                _outcomeReason.clear();
                rewardFrom(go);
            }
            else
            {
                _questGiverResolveElapsedMs += deltaMs;
                _outcomeReason = res == Helper::ApproachResult::NotFound
                    ? "waiting for the live quest-end object near its cached position"
                    : "the live quest-end object is currently not interactable";
                if (TurnInInteractionPolicy::HasResolveTimedOut(
                    _questGiverResolveElapsedMs))
                {
                    retryThroughBrain = true;
                }
            }
        }
        else
        {
            Creature* creature = nullptr;
            auto res = Helper::ApproachCreature(bot, movement, completed.questGiverEntry,
                completed.questGiverGuid,
                TurnInInteractionPolicy::LiveQuestEnderResolveRange, creature);
            if (res == Helper::ApproachResult::Approaching)
            {
                _questGiverResolveElapsedMs = 0;
                _outcomeReason.clear();
                return;
            }
            if (res == Helper::ApproachResult::Ready)
            {
                _questGiverResolveElapsedMs = 0;
                _outcomeReason.clear();
                rewardFrom(creature);
            }
            else
            {
                _questGiverResolveElapsedMs += deltaMs;
                _outcomeReason = res == Helper::ApproachResult::NotFound
                    ? "waiting for the live quest ender near its cached position"
                    : "the live quest ender is currently not interactable";
                if (TurnInInteractionPolicy::HasResolveTimedOut(
                    _questGiverResolveElapsedMs))
                {
                    retryThroughBrain = true;
                }
            }
        }

        _completed = turnInSucceeded || retryThroughBrain;
        if (turnInSucceeded)
            _outcome = ActionOutcome::Succeeded;
        else if (retryThroughBrain)
        {
            _outcome = ActionOutcome::RetryableFailure;
            _failureCategory = inventoryCapacityBlocked
                ? FailureCategory::InventoryCapacity : FailureCategory::Interaction;
            _recoveryDirective = inventoryCapacityBlocked
                ? RecoveryDirective::Replan : RecoveryDirective::RetryLater;
            if (_outcomeReason.empty())
                _outcomeReason = "quest reward or live quest-giver interaction did not complete";
        }
    }

    void TurnInQuestAction::Stop(Player* bot, MovementManager* movement)
    {
        _worldTravel.Stop(bot, movement);
    }
}
