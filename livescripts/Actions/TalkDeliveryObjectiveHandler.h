#pragma once

#include "ObjectiveHandler.h"
#include "Helper/NpcApproachHelper.h"

namespace Actions
{
    struct TalkDeliveryObjectiveHandler
    {
        static constexpr uint32_t InteractionNoProgressTimeoutMs = 30000;

        static ObjectiveOutcome TryHandle(ObjectiveContext& ctx, uint32_t& interactionRetryTimerMs, uint32_t targetAcquireTimerMs)
        {
            if (!ctx.activeQuest.isTalkOrTravelOnly)
                return { ObjectiveResult::NotApplicable };

            if (!Helper::NpcUtils::IsInInteractionRange(ctx.bot,
                ctx.activeQuest.targetPosition.x, ctx.activeQuest.targetPosition.y,
                ctx.activeQuest.targetPosition.z, Constants::QuestInteractionRange))
            {
                ctx.TryMoveTo(ctx.activeQuest.targetPosition.x,
                    ctx.activeQuest.targetPosition.y,
                    ctx.activeQuest.targetPosition.z,
                    "quest_talk_delivery_area");
                return { ObjectiveResult::Handled };
            }

            if (interactionRetryTimerMs == 0)
            {
                if (ctx.movement)
                    ctx.movement->Stop();

                if (ctx.activeQuest.targetKind == Blackboard::QuestTargetKind::GameObject)
                {
                    GameObject* go = nullptr;
                    auto result = Helper::ApproachGameObject(ctx.bot, ctx.movement,
                        ctx.activeQuest.targetNpcEntry, ObjectGuid(),
                        Constants::DefaultNpcSearchRadius, go, false,
                        [&ctx](float x, float y, float z) {
                            return ctx.TryMoveTo(x, y, z,
                                "quest_talk_delivery_gameobject");
                        });
                    if (result == Helper::ApproachResult::Approaching)
                        return { ObjectiveResult::Handled };
                }
                else
                {
                    Creature* creature = nullptr;
                    auto result = Helper::ApproachCreature(ctx.bot, ctx.movement,
                        ctx.activeQuest.targetNpcEntry,
                        Constants::DefaultNpcSearchRadius, creature,
                        Constants::QuestInteractionRange, false,
                        [&ctx](float x, float y, float z) {
                            return ctx.TryMoveTo(x, y, z,
                                "quest_talk_delivery_creature");
                        });
                    if (result == Helper::ApproachResult::Approaching)
                        return { ObjectiveResult::Handled };
                }
                interactionRetryTimerMs = 2000;
            }

            if (ctx.bot->CanCompleteQuest(ctx.activeQuest.questId))
                ctx.bot->CompleteQuest(ctx.activeQuest.questId);

            if (ctx.bot->GetQuestStatus(ctx.activeQuest.questId) == QUEST_STATUS_COMPLETE)
                return { ObjectiveResult::Completed };

            if (targetAcquireTimerMs >= InteractionNoProgressTimeoutMs)
                return { ObjectiveResult::Failed, "scripted talk/delivery interaction produced no quest progress" };

            return { ObjectiveResult::Handled };
        }
    };
}
