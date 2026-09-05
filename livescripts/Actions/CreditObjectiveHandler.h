#pragma once

#include "ObjectiveHandler.h"
#include "Helper/NpcApproachHelper.h"
#include "Helper/Constants.h"
#include "Helper/SpellUtils.h"
#include "Diagnostics/BotTrace.h"
#include "Item.h"
#include "Log.h"
#include "Spell.h"

namespace Actions
{
    struct CreditObjectiveHandler
    {
        static ObjectiveOutcome TryCastOnCreature(ObjectiveContext& ctx, uint32_t& interactionRetryTimerMs)
        {
            for (const auto& objective : ctx.activeQuest.objectives)
            {
                if (objective.currentCount >= objective.requiredCount ||
                    objective.type != Blackboard::QuestObjectiveType::CastOnCreature)
                    continue;

                uint32_t interactionEntry = objective.interactionEntry != 0
                    ? objective.interactionEntry : objective.targetEntry;
                Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntryAnyReaction(
                    ctx.bot, interactionEntry, Constants::TacticalScanRadius);
                if (!creature || !creature->IsAlive())
                    continue;

                if (ctx.bot->GetDistance(creature) > Constants::QuestInteractionRange ||
                    !ctx.bot->IsWithinLOSInMap(creature))
                {
                    ctx.TryMoveTo(creature->GetPositionX(),
                        creature->GetPositionY(), creature->GetPositionZ(),
                        "quest_cast_creature");
                    return { ObjectiveResult::Handled };
                }

                if (interactionRetryTimerMs == 0 && objective.sourceItemId != 0)
                {
                    if (ctx.movement)
                        ctx.movement->Stop();
                    Item* sourceItem = ctx.bot->GetItemByEntry(objective.sourceItemId);
                    if (!sourceItem)
                        return { ObjectiveResult::Handled };

                    SpellCastTargets targets;
                    if (objective.sourceSpellTargetsEntity)
                        targets.SetUnitTarget(creature);
                    else
                        targets.SetUnitTarget(ctx.bot);
                    ctx.bot->SetSelection(creature->GetGUID());
                    ctx.bot->SetFacingToObject(creature);
                    bool castStarted = Helper::SpellUtils::TryUseResolvedQuestItem(
                        ctx.bot, sourceItem, objective.sourceSpellId, targets);
                    interactionRetryTimerMs = castStarted ? 2000 : 1000;

                    if (castStarted && Diagnostics::BotTrace::ShouldLog(ctx.bot))
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' used source Item {} (Spell {}) on interaction Creature {} for credited Entry {} in quest {}",
                            ctx.bot->GetName(), objective.sourceItemId,
                            objective.sourceSpellId, creature->GetEntry(),
                            objective.targetEntry, ctx.activeQuest.questId);
                    }
                }
                return { ObjectiveResult::Handled };
            }
            return { ObjectiveResult::NotApplicable };
        }

        static ObjectiveOutcome TryTalkToCreature(ObjectiveContext& ctx, uint32_t& interactionRetryTimerMs)
        {
            for (const auto& objective : ctx.activeQuest.objectives)
            {
                if (objective.currentCount >= objective.requiredCount ||
                    objective.type != Blackboard::QuestObjectiveType::TalkToCreature)
                    continue;

                Creature* creature = Helper::NpcUtils::FindNearbyCreatureByEntry(
                    ctx.bot, objective.targetEntry, Constants::TacticalScanRadius);
                if (!creature || ctx.bot->IsValidAttackTarget(creature))
                    continue;

                Creature* prepared = nullptr;
                auto result = Helper::ApproachCreature(ctx.bot, ctx.movement,
                    objective.targetEntry, Constants::TacticalScanRadius,
                    prepared, Constants::QuestInteractionRange, false,
                    [&ctx](float x, float y, float z) {
                        return ctx.TryMoveTo(x, y, z,
                            "quest_talk_creature");
                    });
                if (result == Helper::ApproachResult::Approaching)
                    return { ObjectiveResult::Handled };
                if (result == Helper::ApproachResult::Ready && interactionRetryTimerMs == 0)
                {
                    if (ctx.movement)
                        ctx.movement->Stop();
                    interactionRetryTimerMs = 2000;
                }
                return { ObjectiveResult::Handled };
            }
            return { ObjectiveResult::NotApplicable };
        }
    };
}
