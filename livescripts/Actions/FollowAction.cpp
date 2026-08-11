#include "FollowAction.h"
#include "ObjectAccessor.h"

namespace Actions
{
    FollowAction::FollowAction(ObjectGuid targetGuid, float distance, float angle)
        : _targetGuid(targetGuid), _distance(distance), _angle(angle)
    {
    }

    void FollowAction::UpdateFollow(Player* bot, MovementManager* movement)
    {
        if (movement && bot && _targetGuid)
        {
            Unit* target = ObjectAccessor::GetUnit(*bot, _targetGuid);
            if (target && target->IsInWorld() && target->IsAlive())
            {
                movement->Follow(target, _distance, _angle);
            }
            else
            {
                _completed = true;
            }
        }
    }

    void FollowAction::Start(Player* bot, MovementManager* movement)
    {
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

    bool FollowAction::IsComplete() const
    {
        return _completed || !_targetGuid;
    }
}
