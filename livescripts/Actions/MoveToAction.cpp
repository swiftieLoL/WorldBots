#include "MoveToAction.h"
#include "Helper/MathUtils.h"

namespace Actions
{
    MoveToAction::MoveToAction(float x, float y, float z, uint32_t mapId)
        : _x(x), _y(y), _z(z), _mapId(mapId), _started(false), _completed(false)
    {
    }

    void MoveToAction::Start(Player* bot, MovementManager* movement)
    {
        if (_mapId != 0 && (!bot || bot->GetMapId() != _mapId))
        {
            _completed = true;
            return;
        }

        if (movement)
        {
            movement->MoveTo(_x, _y, _z, BotMovementState::Moving, true);
            _started = true;
        }
    }

    void MoveToAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/)
    {
        if (movement && bot)
        {
            if (_mapId != 0 && bot->GetMapId() != _mapId)
            {
                _completed = true;
                return;
            }

            if (!_started)
            {
                movement->MoveTo(_x, _y, _z, BotMovementState::Moving, false);
                _started = true;
            }
            else if (movement->GetState() == BotMovementState::Idle)
            {
                if (Helper::IsIn3DRange(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), _x, _y, _z, 3.0f))
                {
                    _completed = true;
                }
                else
                {
                    movement->MoveTo(_x, _y, _z, BotMovementState::Moving, false);
                }
            }
        }
    }

    void MoveToAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
        {
            movement->Stop();
        }
    }

    bool MoveToAction::IsComplete() const
    {
        return _completed;
    }
}
