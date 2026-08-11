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

namespace Actions
{
    std::unique_ptr<BotAction> ActionFactory::CreateAction(const Brain::ActionRequest& request)
    {
        switch (request.goal)
        {
            case Brain::BotGoal::Combat:
                return std::make_unique<CombatAction>(std::get<Brain::TargetActionRequest>(request.payload).targetGuid);

            case Brain::BotGoal::Loot:
                return std::make_unique<LootAction>();

            case Brain::BotGoal::Vendor:
                return std::make_unique<VendorAction>();

            case Brain::BotGoal::TownRun:
                return std::make_unique<TownRunAction>(std::get<Brain::TownRunActionRequest>(request.payload).plan);

            case Brain::BotGoal::TurnInQuest:
                return std::make_unique<TurnInQuestAction>(std::get<Brain::QuestActionRequest>(request.payload).questId);

            case Brain::BotGoal::ProgressQuest:
                return std::make_unique<ProgressQuestAction>(std::get<Brain::QuestActionRequest>(request.payload).questId);

            case Brain::BotGoal::AcceptQuest:
                return std::make_unique<AcceptQuestAction>(std::get<Brain::QuestActionRequest>(request.payload).questId);

            case Brain::BotGoal::MoveToNpc:
            {
                const Common::PositionInfo& destination = std::get<Brain::MoveActionRequest>(request.payload).destination;
                return std::make_unique<MoveToAction>(destination.x, destination.y, destination.z, destination.mapId);
            }

            case Brain::BotGoal::FollowTarget:
                return std::make_unique<FollowAction>(std::get<Brain::TargetActionRequest>(request.payload).targetGuid);

            case Brain::BotGoal::Flee:
                return std::make_unique<FleeAction>(std::get<Brain::TargetActionRequest>(request.payload).targetGuid);

            case Brain::BotGoal::Resurrect:
                return std::make_unique<ResurrectAction>();

            case Brain::BotGoal::Unstuck:
                return std::make_unique<UnstuckAction>(std::get<Brain::UnstuckActionRequest>(request.payload).deadlyQuestId);

            case Brain::BotGoal::Rest:
                return std::make_unique<RestAction>();

            case Brain::BotGoal::Wander:
            {
                const Brain::WanderActionRequest& wander = std::get<Brain::WanderActionRequest>(request.payload);
                return std::make_unique<WanderAction>(wander.origin.x, wander.origin.y, wander.origin.z,
                    wander.radius, wander.suppressedQuests);
            }

            case Brain::BotGoal::Grind:
            {
                const Brain::GrindActionRequest& grind = std::get<Brain::GrindActionRequest>(request.payload);
                return std::make_unique<GrindAction>(grind.minLevelOffset, grind.maxLevelOffset);
            }

            case Brain::BotGoal::Idle:
            default:
                return std::make_unique<IdleAction>();
        }
    }
}
