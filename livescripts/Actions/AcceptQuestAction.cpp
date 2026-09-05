#include <unordered_map>
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "QuestAction.h"
static std::unordered_map<uint64, std::unordered_map<uint32, uint32>> s_acceptQuestLastLogTime;

#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Helper/NpcFinder.h"
#include "Helper/Constants.h"
#include "Helper/InventoryUtils.h"
#include "Helper/QuestUtils.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/NpcApproachHelper.h"

namespace Actions
{
    // ==========================================
    // ACCEPT QUEST ACTION
    // ==========================================
    void AcceptQuestAction::Start(Player* /*bot*/, MovementManager* /*movement*/)
    {
        ResetOutcome();
        _lastKnownQuest = {};
        _hasLastKnownQuest = false;
        _availabilityGrace.Reset();
        _failsafe.Reset();
        _worldTravel.Reset();
    }

    void AcceptQuestAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "bot or movement manager was unavailable during quest acceptance",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        // The quest snapshot refreshes once per second. Some breadcrumb and
        // talk quests become active or complete immediately when accepted, so
        // the old "available" row can survive into the next action. Treat the
        // live quest state as authoritative instead of attempting to accept it
        // again and reporting a false eligibility failure.
        if (_selectedQuestId != 0 &&
            bot->GetQuestStatus(_selectedQuestId) != QUEST_STATUS_NONE)
        {
            _outcome = ActionOutcome::Succeeded;
            _completed = true;
            return;
        }

        const Blackboard::KnownQuest* selected = nullptr;
        for (const auto& candidate : blackboard.quest.availableQuests)
        {
            if (candidate.questId == _selectedQuestId)
            {
                selected = &candidate;
                break;
            }
        }
        if (selected)
        {
            _lastKnownQuest = *selected;
            _hasLastKnownQuest = true;
        }

        // Capture the selected quest once, then let the action own that stable
        // destination. The one-second sensor snapshot may omit it while nearby
        // quest givers are discovered or grids unload during a long journey.
        // Only use the grace period before this action has observed its initial
        // candidate; live quest status and interaction eligibility remain the
        // authoritative completion checks after that.
        if (!_hasLastKnownQuest &&
            !_availabilityGrace.Observe(selected != nullptr, deltaMs))
        {
            _outcome = ActionOutcome::Interrupted;
            _failureCategory = FailureCategory::Transient;
            _recoveryDirective = RecoveryDirective::None;
            _outcomeReason = "selected quest remained absent beyond the sensing grace period";
            _completed = true;
            return;
        }
        if (!_hasLastKnownQuest)
            return;

        const auto& available = selected ? *selected : _lastKnownQuest;

        if (!available.hasQuestGiverPosition)
        {
            _outcome = ActionOutcome::Blocked;
            _failureCategory = FailureCategory::Navigation;
            _recoveryDirective = RecoveryDirective::RetryLater;
            _outcomeReason = "quest giver position is missing";
            _completed = true;
            return;
        }

        // Only the final interaction approach bypasses WorldTravel. Every
        // longer quest leg must yield for the same per-bot danger scan used by
        // continent-scale travel.
        if (_worldTravel.IsActive() ||
            Travel::WorldTravel::NeedsTravel(bot, available.questGiverPosition, 20.0f))
        {
            Travel::TravelResult travelResult = _worldTravel.Update(bot, movement,
                available.questGiverPosition, deltaMs, _dangerAreas,
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

        constexpr uint32_t TravelHardTimeoutMs = 10 * 60 * 1000;
        if (_failsafe.CheckMovementProgress(deltaMs, Constants::FailsafeMovingTimeoutMs,
            Constants::FailsafeIdleTimeoutMs, TravelHardTimeoutMs, movement->HasPath(),
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            _outcome = ActionOutcome::Blocked;
            _failureCategory = FailureCategory::Navigation;
            _recoveryDirective = RecoveryDirective::RetryLater;
            _outcomeReason = std::string("quest giver travel failed: ") +
                _failsafe.GetMovementFailureReason();
            _completed = true;
            return;
        }

        if (Helper::NpcUtils::IsInInteractionRange(bot, available.questGiverPosition.x, available.questGiverPosition.y, available.questGiverPosition.z, Constants::QuestInteractionRange))
        {
            Quest const* qTemplate = sObjectMgr->GetQuestTemplate(available.questId);
            if (!qTemplate)
            {
                Finish(ActionOutcome::Unsupported, "quest template is missing during acceptance",
                    FailureCategory::ContentUnsupported, RecoveryDirective::RetryLater);
                return;
            }

            bool interactionAttempted = false;
            bool liveEligibilityChanged = false;
            bool sourceItemCapacityBlocked = false;
            auto finishApproachFailure = [&](Helper::ApproachResult result,
                const char* targetKind) {
                bool navigationFailure = result == Helper::ApproachResult::Invalid &&
                    movement->GetLastPathFailure() != BotPathFailure::None;
                std::string reason = std::string(targetKind) +
                    (result == Helper::ApproachResult::NotFound
                        ? " quest giver was not present near its cached position"
                        : " quest giver approach failed");
                if (navigationFailure)
                {
                    reason += ": ";
                    reason += movement->GetLastPathFailureName();
                    reason += " (path flags ";
                    reason += std::to_string(movement->GetLastPathFlags());
                    reason += ")";
                }
                Finish(navigationFailure ? ActionOutcome::Blocked
                        : ActionOutcome::RetryableFailure,
                    std::move(reason), navigationFailure
                        ? FailureCategory::Navigation : FailureCategory::Interaction,
                    RecoveryDirective::RetryLater);
            };

            if (qTemplate)
            {
                auto acceptFrom = [&](Object* questGiver) {
                    interactionAttempted = true;
                    bool canTakeQuest = bot->CanTakeQuest(qTemplate, true);
                    bool canAddQuest = bot->CanAddQuest(qTemplate, true);
                    bool canCarrySourceItem = Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate);
                    if (canTakeQuest && canAddQuest && canCarrySourceItem)
                    {
                        bot->AddQuestAndCheckCompletion(qTemplate, questGiver);

                        if (bot->GetQuestStatus(available.questId) != QUEST_STATUS_NONE)
                        {
                            if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                            {
                                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' (GUID: {}) ACCEPTED quest {} ('{}')",
                                    bot->GetName(), bot->GetGUID().GetCounter(), available.questId, qTemplate->GetTitle());
                            }
                        }
                        else
                        {
                            uint64 key = bot->GetGUID().GetRawValue();
                            uint32 now = getMSTime();
                            uint32& lastLog = s_acceptQuestLastLogTime[key][available.questId];
                            if ((lastLog == 0 || now - lastLog > 5000) &&
                                Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                            {
                                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' failed to accept quest {} ('{}') after CanAddQuest passed (quest log/source-item state changed during interaction)",
                                    bot->GetName(), available.questId, qTemplate->GetTitle());
                                lastLog = now;
                            }
                        }
                    }
                    else
                    {
                        liveEligibilityChanged = !canTakeQuest;
                        sourceItemCapacityBlocked = !canCarrySourceItem;
                        uint64 key = bot->GetGUID().GetRawValue();
                        uint32 now = getMSTime();
                        uint32& lastLog = s_acceptQuestLastLogTime[key][available.questId];
                        if ((lastLog == 0 || now - lastLog > 5000) &&
                            Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                        {
                            TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot accept quest {} ('{}'): CanTakeQuest={} CanAddQuest={} CanCarrySourceItem={} (FreeBagSlots: {})",
                                bot->GetName(), available.questId, qTemplate->GetTitle(),
                                canTakeQuest ? "true" : "false",
                                canAddQuest ? "true" : "false",
                                canCarrySourceItem ? "true" : "false",
                                Helper::InventoryUtils::CountFreeBagSlots(bot));
                            lastLog = now;
                        }
                    }
                };

                if (available.questGiverKind == Blackboard::QuestTargetKind::GameObject)
                {
                    GameObject* go = nullptr;
                    auto res = Helper::ApproachGameObject(bot, movement, available.questGiverEntry,
                        available.questGiverGuid, Constants::DefaultNpcSearchRadius, go);
                    if (res == Helper::ApproachResult::Approaching) return;
                    if (res == Helper::ApproachResult::Ready)
                        acceptFrom(go);
                    else
                    {
                        finishApproachFailure(res, "game object");
                        return;
                    }
                }
                else
                {
                    Creature* creature = nullptr;
                    auto res = Helper::ApproachCreature(bot, movement, available.questGiverEntry,
                        available.questGiverGuid, Constants::DefaultNpcSearchRadius, creature);
                    if (res == Helper::ApproachResult::Approaching) return;
                    if (res == Helper::ApproachResult::Ready)
                        acceptFrom(creature);
                    else
                    {
                        finishApproachFailure(res, "creature");
                        return;
                    }
                }
            }

            // Let the next blackboard refresh decide whether the quest remains
            // available. This prevents a failed acceptance from being treated
            // as a successful state transition while still allowing inventory
            // or quest-log changes to make it eligible later.
            _completed = interactionAttempted;
            if (_completed)
            {
                bool accepted = bot->GetQuestStatus(available.questId) != QUEST_STATUS_NONE;
                _outcome = accepted ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure;
                if (!accepted)
                {
                    if (liveEligibilityChanged)
                    {
                        // The one-second quest snapshot was valid when the
                        // action was selected, but live prerequisite/status
                        // state changed before interaction. Replan without
                        // suppressing a quest that may already have advanced.
                        _outcome = ActionOutcome::Interrupted;
                        _failureCategory = FailureCategory::Transient;
                        _recoveryDirective = RecoveryDirective::None;
                        _outcomeReason = "quest eligibility changed before interaction";
                    }
                    else if (sourceItemCapacityBlocked)
                    {
                        _failureCategory = FailureCategory::InventoryCapacity;
                        _recoveryDirective = RecoveryDirective::Replan;
                        _outcomeReason = "quest source item could not fit at interaction time";
                    }
                    else
                    {
                        _failureCategory = FailureCategory::Interaction;
                        _recoveryDirective = RecoveryDirective::RetryLater;
                        _outcomeReason = "quest giver interaction did not add the quest";
                    }
                }
            }
        }
        else
        {
            if (!movement->MoveTo(available.questGiverPosition.x,
                available.questGiverPosition.y, available.questGiverPosition.z,
                BotMovementState::Moving, false))
            {
                std::string reason = "quest giver movement failed: ";
                reason += movement->GetLastPathFailureName();
                reason += " (path flags ";
                reason += std::to_string(movement->GetLastPathFlags());
                reason += ")";
                Finish(ActionOutcome::Blocked, std::move(reason),
                    FailureCategory::Navigation, RecoveryDirective::RetryLater);
            }
        }
    }

    void AcceptQuestAction::Stop(Player* bot, MovementManager* movement)
    {
        _worldTravel.Stop(bot, movement);
    }

    void AcceptQuestAction::ClearBotState(ObjectGuid botGuid)
    {
        if (botGuid)
            s_acceptQuestLastLogTime.erase(botGuid.GetRawValue());
    }

    void AcceptQuestAction::ClearAllState()
    {
        s_acceptQuestLastLogTime.clear();
    }
}
