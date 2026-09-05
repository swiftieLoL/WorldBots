#pragma once

#include "ObjectiveHandler.h"
#include "Helper/NpcFinder.h"

namespace Actions
{
    struct ExplorationObjectiveHandler
    {
        static ObjectiveOutcome TryHandle(ObjectiveContext& ctx)
        {
            if (!ctx.activeQuest.requiresExploration)
                return { ObjectiveResult::NotApplicable };

            if (!Helper::NpcUtils::IsInInteractionRange(ctx.bot,
                ctx.activeQuest.targetPosition.x, ctx.activeQuest.targetPosition.y,
                ctx.activeQuest.targetPosition.z, 10.0f))
            {
                ctx.TryMoveTo(ctx.activeQuest.targetPosition.x,
                    ctx.activeQuest.targetPosition.y,
                    ctx.activeQuest.targetPosition.z,
                    "quest_exploration");
                return { ObjectiveResult::Handled };
            }

            ctx.bot->AreaExploredOrEventHappens(ctx.activeQuest.questId);
            if (ctx.bot->GetQuestStatus(ctx.activeQuest.questId) == QUEST_STATUS_COMPLETE)
                return { ObjectiveResult::Completed };

            return { ObjectiveResult::Handled };
        }
    };
}
