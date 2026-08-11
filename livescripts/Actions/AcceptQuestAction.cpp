#include <unordered_map>
#include "QuestAction.h"
static std::unordered_map<uint64, uint32> s_acceptQuestLastLogTime;

#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Helper/NpcFinder.h"
#include "Helper/Constants.h"
#include "Helper/InventoryUtils.h"
#include "Helper/QuestUtils.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"

namespace Actions
{
    // ==========================================
    // ACCEPT QUEST ACTION
    // ==========================================
    void AcceptQuestAction::Start(Player* /*bot*/, MovementManager* /*movement*/)
    {
        _completed = false;
        _outcome = ActionOutcome::Running;
        _outcomeReason.clear();
        _failsafe.Reset();
    }

    void AcceptQuestAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !movement)
        {
            _completed = true;
            return;
        }

        if (blackboard.quest.availableQuests.empty())
        {
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
        if (!selected)
        {
            _completed = true;
            return;
        }
        const auto& available = *selected;

        if (!available.hasQuestGiverPosition || available.questGiverPosition.mapId != bot->GetMapId())
        {
            TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot accept quest {} because its quest giver is on map {} while the bot is on map {}",
                bot->GetName(), available.questId, available.questGiverPosition.mapId, bot->GetMapId());
            _outcome = ActionOutcome::Blocked;
            _outcomeReason = "quest giver is missing or requires cross-map travel";
            _completed = true;
            return;
        }

        constexpr uint32_t TravelHardTimeoutMs = 10 * 60 * 1000;
        if (_failsafe.CheckMovementProgress(deltaMs, Constants::FailsafeMovingTimeoutMs,
            Constants::FailsafeIdleTimeoutMs, TravelHardTimeoutMs, movement->HasPath(),
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            _outcome = ActionOutcome::Blocked;
            _outcomeReason = "quest giver travel made no progress or exceeded the hard accept timeout";
            _completed = true;
            return;
        }

        if (Helper::NpcUtils::IsInInteractionRange(bot, available.questGiverPosition.x, available.questGiverPosition.y, available.questGiverPosition.z, Constants::QuestInteractionRange))
        {
            Quest const* qTemplate = sObjectMgr->GetQuestTemplate(available.questId);
            bool interactionAttempted = false;
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
                            if (Diagnostics::BotTrace::ShouldLog(bot))
                            {
                                TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' (GUID: {}) ACCEPTED quest {} ('{}')",
                                    bot->GetName(), bot->GetGUID().GetCounter(), available.questId, qTemplate->GetTitle());
                            }
                        }
                        else
                        {
                            {
                            uint64 key = (static_cast<uint64>(bot->GetGUID().GetRawValue()) << 32) | available.questId;
                            uint32 now = getMSTime();
                            auto it = s_acceptQuestLastLogTime.find(key);
                            if (it == s_acceptQuestLastLogTime.end() || now - it->second > 5000) {
                                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' failed to accept quest {} ('{}') after CanAddQuest passed (quest log/source-item state changed during interaction)",
                                    bot->GetName(), available.questId, qTemplate->GetTitle());
                                s_acceptQuestLastLogTime[key] = now;
                            }
                        }
                        }
                    }
                    else
                    {
                        {
                            uint64 key = (static_cast<uint64>(bot->GetGUID().GetRawValue()) << 32) | available.questId;
                            uint32 now = getMSTime();
                            auto it = s_acceptQuestLastLogTime.find(key);
                            if (it == s_acceptQuestLastLogTime.end() || now - it->second > 5000) {
                                TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' cannot accept quest {} ('{}'): CanTakeQuest={} CanAddQuest={} CanCarrySourceItem={} (FreeBagSlots: {})",
                                    bot->GetName(), available.questId, qTemplate->GetTitle(),
                                    canTakeQuest ? "true" : "false",
                                    canAddQuest ? "true" : "false",
                                    canCarrySourceItem ? "true" : "false",
                                    Helper::InventoryUtils::CountFreeBagSlots(bot));
                                s_acceptQuestLastLogTime[key] = now;
                            }
                        }
                    }
                };

                if (available.questGiverKind == Blackboard::QuestTargetKind::GameObject)
                {
                    GameObject* go = available.questGiverGuid
                        ? bot->GetMap()->GetGameObject(available.questGiverGuid)
                        : bot->FindNearestGameObject(available.questGiverEntry, Constants::DefaultNpcSearchRadius);
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
                            acceptFrom(go);
                        }
                    }
                }
                else
                {
                    Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntry(bot,
                        available.questGiverEntry, Constants::DefaultNpcSearchRadius);
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
                            acceptFrom(creature);
                        }
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
                    _outcomeReason = "quest giver interaction did not add the quest";
            }
        }
        else
        {
            movement->MoveTo(available.questGiverPosition.x, available.questGiverPosition.y, available.questGiverPosition.z, BotMovementState::Moving, false);
        }
    }

    void AcceptQuestAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
        {
            movement->Stop();
        }
    }
}
