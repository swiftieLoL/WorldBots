#include "ActionFactory.h"
#include "IdleAction.h"
#include "MoveToAction.h"
#include "FollowAction.h"
#include "WanderAction.h"
#include "GrindAction.h"
#include "CombatAction.h"
#include "FleeAction.h"
#include "LootAction.h"
#include "VendorAction.h"
#include "RestAction.h"
#include "ResurrectAction.h"
#include "QuestAction.h"
#include "UnstuckAction.h"
#include "TownRunAction.h"
#include "RevivePartyMemberAction.h"

namespace Actions
{
    std::unique_ptr<BotAction> ActionFactory::CreateAction(const Brain::ActionRequest& request)
    {
        switch (request.goal)
        {
            case Brain::BotGoal::Combat:
                if (auto const* target = std::get_if<Brain::TargetActionRequest>(&request.payload))
                    return std::make_unique<CombatAction>(target->targetGuid);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::Loot:
                return std::make_unique<LootAction>();

            case Brain::BotGoal::Vendor:
                return std::make_unique<VendorAction>();

            case Brain::BotGoal::TownRun:
                if (auto const* town = std::get_if<Brain::TownRunActionRequest>(&request.payload))
                    return std::make_unique<TownRunAction>(town->plan, town->suppressedNpcEntries,
                        town->maxVendorTravelDistance, town->dangerAreas);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::TurnInQuest:
                if (auto const* quest = std::get_if<Brain::QuestActionRequest>(&request.payload))
                    return std::make_unique<TurnInQuestAction>(quest->questId, quest->dangerAreas);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::ProgressQuest:
                if (auto const* quest = std::get_if<Brain::QuestActionRequest>(&request.payload))
                    return std::make_unique<ProgressQuestAction>(quest->questId, quest->dangerAreas);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::AcceptQuest:
                if (auto const* quest = std::get_if<Brain::QuestActionRequest>(&request.payload))
                    return std::make_unique<AcceptQuestAction>(quest->questId, quest->dangerAreas);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::MoveToNpc:
                if (auto const* move = std::get_if<Brain::MoveActionRequest>(&request.payload))
                    return std::make_unique<MoveToAction>(move->destination.x, move->destination.y, move->destination.z, move->destination.mapId);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::FollowTarget:
                if (auto const* follow = std::get_if<Brain::FollowActionRequest>(&request.payload))
                    return std::make_unique<FollowAction>(follow->targetGuid, follow->distance, follow->angle);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::RevivePartyMember:
                if (auto const* target = std::get_if<Brain::TargetActionRequest>(&request.payload))
                    return std::make_unique<RevivePartyMemberAction>(target->targetGuid);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::Flee:
                if (auto const* target = std::get_if<Brain::TargetActionRequest>(&request.payload))
                    return std::make_unique<FleeAction>(target->targetGuid);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::Resurrect:
                return std::make_unique<ResurrectAction>();

            case Brain::BotGoal::Unstuck:
                if (auto const* unstuck = std::get_if<Brain::UnstuckActionRequest>(&request.payload))
                    return std::make_unique<UnstuckAction>(unstuck->deadlyQuestId,
                        unstuck->progressionRecovery);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::Rest:
                return std::make_unique<RestAction>();

            case Brain::BotGoal::Wander:
                if (auto const* wander = std::get_if<Brain::WanderActionRequest>(&request.payload))
                    return std::make_unique<WanderAction>(wander->origin.x, wander->origin.y, wander->origin.z,
                        wander->radius, wander->suppressedQuests,
                        wander->suppressedDestinations);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::Grind:
                if (auto const* grind = std::get_if<Brain::GrindActionRequest>(&request.payload))
                    return std::make_unique<GrindAction>(grind->minLevelOffset, grind->maxLevelOffset,
                        grind->suppressedSpawnIds, grind->suppressedCreatureEntries,
                        grind->suppressedDestinations, grind->dangerAreas);
                return std::make_unique<IdleAction>();

            case Brain::BotGoal::Idle:
            case Brain::BotGoal::WaitForPartyResurrection:
            default:
                return std::make_unique<IdleAction>();
        }
    }
}
