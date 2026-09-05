#include "FollowAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "ObjectAccessor.h"

namespace Actions
{
    FollowAction::FollowAction(ObjectGuid targetGuid, float distance, float angle)
        : _targetGuid(targetGuid), _distance(distance), _angle(angle)
    {
    }

    void FollowAction::UpdateFollow(Player* bot, MovementManager* movement)
    {
        if (!movement || !bot || !bot->IsInWorld() || !_targetGuid)
        {
            Finish(ActionOutcome::RetryableFailure, "follow context was unavailable",
                movement ? FailureCategory::Transient : FailureCategory::Navigation,
                RecoveryDirective::RetryLater);
            return;
        }

        Unit* target = ObjectAccessor::GetUnit(*bot, _targetGuid);
        if (target && target->IsInWorld() && target->IsAlive())
        {
            movement->Follow(target, _distance, _angle);
        }
        else
        {
            Finish(ActionOutcome::Succeeded, "follow target is no longer available");
        }
    }

    void FollowAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        UpdateFollow(bot, movement);
    }

    void FollowAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/)
    {
        UpdateFollow(bot, movement);
    }

    void FollowAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
        {
            movement->Stop();
        }
    }
}
