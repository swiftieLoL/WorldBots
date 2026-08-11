#include "FleeAction.h"

#include "ObjectAccessor.h"
#include "Helper/MathUtils.h"
#include "Map.h"
#include <cmath>

namespace Actions
{
    FleeAction::FleeAction(ObjectGuid threatGuid)
        : _threatGuid(threatGuid)
    {
    }

    void FleeAction::Start(Player* bot, MovementManager* movement)
    {
        _elapsedMs = 0;
        _completed = false;

        if (!bot || !bot->IsInWorld() || !_threatGuid)
        {
            _completed = true;
            return;
        }

        if (movement)
            movement->Stop();
    }

    void FleeAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !movement)
        {
            _completed = true;
            return;
        }

        _elapsedMs += deltaMs;
        if (_elapsedMs >= 10000)
        {
            movement->Stop();
            _completed = true;
            return;
        }

        Unit* threat = ObjectAccessor::GetUnit(*bot, _threatGuid);
        if (!threat || !threat->IsInWorld() || !threat->IsAlive() || threat->GetMap() != bot->GetMap())
        {
            movement->Stop();
            _completed = true;
            return;
        }

        if (bot->GetDistance(threat) >= 25.0f && movement->GetState() == BotMovementState::Idle)
        {
            _completed = true;
            return;
        }

        // Keep the current escape leg stable until it completes. Recomputing
        // a point 15 yards ahead from the bot's new position every 50 ms makes
        // the destination drift continuously and repeatedly rebuilds paths.
        if (movement->GetState() == BotMovementState::Fleeing)
            return;

        float dx = bot->GetPositionX() - threat->GetPositionX();
        float dy = bot->GetPositionY() - threat->GetPositionY();
        float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01f)
        {
            dx = std::cos(bot->GetOrientation());
            dy = std::sin(bot->GetOrientation());
            length = 1.0f;
        }

        constexpr float fleeDistance = 15.0f;
        float targetX = bot->GetPositionX() + (dx / length) * fleeDistance;
        float targetY = bot->GetPositionY() + (dy / length) * fleeDistance;
        float targetZ = bot->GetPositionZ();

        if (Map* map = bot->GetMap())
        {
            float floorZ = map->GetHeight(bot->GetPhaseMask(), targetX, targetY, targetZ, true, 50.0f);
            if (floorZ > -500.0f && !std::isnan(floorZ))
                targetZ = floorZ;
        }

        // Start one stable escape leg. A new leg is calculated only after the
        // current spline finishes and MovementManager returns to Idle.
        movement->MoveTo(targetX, targetY, targetZ, BotMovementState::Fleeing, false);
    }

    void FleeAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
            movement->Stop();
    }
}
