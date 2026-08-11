#include "Globals/ObjectMgr.h"
#include "MovementManager.h"
#include "Helper/MathUtils.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include <cmath>
#include <vector>

MovementManager::MovementManager(Player* bot)
{
    if (bot)
        _botGuid = bot->GetGUID();
}

MovementManager::~MovementManager()
{
    Shutdown();
}

bool MovementManager::HasPath() const
{
    return _hasDestination && (_state == BotMovementState::Moving || _state == BotMovementState::Chasing ||
        _state == BotMovementState::Following || _state == BotMovementState::Fleeing);
}

const char* MovementManager::GetStateName() const
{
    switch (_state)
    {
        case BotMovementState::Idle: return "Idle";
        case BotMovementState::Moving: return "Moving";
        case BotMovementState::Following: return "Following";
        case BotMovementState::Chasing: return "Chasing";
        case BotMovementState::Fleeing: return "Fleeing";
        case BotMovementState::Stuck: return "Stuck";
        default: return "Unknown";
    }
}

const char* MovementManager::GetExternalControlModeName() const
{
    switch (_externalControlMode)
    {
        case BotExternalControlMode::None: return "None";
        case BotExternalControlMode::MoveTo: return "MoveTo";
        case BotExternalControlMode::Follow: return "Follow";
        case BotExternalControlMode::Chase: return "Chase";
        default: return "Unknown";
    }
}

Player* MovementManager::ResolveBot() const
{
    Player* bot = _botGuid ? ObjectAccessor::FindPlayer(_botGuid) : nullptr;
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return nullptr;
    return bot;
}

void MovementManager::Stop()
{
    Player* bot = ResolveBot();
    if (!bot || IsExternallyControlled())
        return;
    StopInternal(bot);
}

void MovementManager::StopExternal()
{
    Player* bot = ResolveBot();
    _externalControlMode = BotExternalControlMode::None;
    if (bot)
        StopInternal(bot);
}

void MovementManager::Shutdown()
{
    if (Player* bot = ResolveBot())
        StopInternal(bot);

    _externalControlMode = BotExternalControlMode::None;
    _target.Clear();
    _hasDestination = false;
    _state = BotMovementState::Idle;
}

void MovementManager::Follow(Unit* target, float distance, float angle)
{
    Player* bot = ResolveBot();
    if (bot && !IsExternallyControlled())
        FollowInternal(bot, target, distance, angle);
}

void MovementManager::FollowExternal(Unit* target, float distance, float angle)
{
    Player* bot = ResolveBot();
    if (bot && FollowInternal(bot, target, distance, angle))
        _externalControlMode = BotExternalControlMode::Follow;
}

void MovementManager::Chase(Unit* target)
{
    Player* bot = ResolveBot();
    if (bot && !IsExternallyControlled())
        ChaseInternal(bot, target);
}

void MovementManager::ChaseExternal(Unit* target)
{
    Player* bot = ResolveBot();
    if (bot && ChaseInternal(bot, target))
        _externalControlMode = BotExternalControlMode::Chase;
}

bool MovementManager::MoveTo(float x, float y, float z, BotMovementState state, bool force)
{
    Player* bot = ResolveBot();
    if (!bot || IsExternallyControlled())
        return false;
    return MoveToInternal(bot, x, y, z, state, force);
}

bool MovementManager::MoveToExternal(float x, float y, float z, BotMovementState state, bool force)
{
    Player* bot = ResolveBot();
    if (!bot)
        return false;
    bool started = MoveToInternal(bot, x, y, z, state, force);
    if (started)
        _externalControlMode = BotExternalControlMode::MoveTo;
    return started;
}

void MovementManager::Update(uint32 diff)
{
    Player* bot = ResolveBot();
    if (!bot)
        return;

    _pathRetryCooldownMs = _pathRetryCooldownMs > diff ? _pathRetryCooldownMs - diff : 0;
    if (!_hasDestination)
    {
        // External point movement is a finite lease. If path creation failed
        // or an earlier update finalized the route, return control to the
        // brain. Follow and Chase remain explicit leases until BotStop.
        if (_externalControlMode == BotExternalControlMode::MoveTo)
            _externalControlMode = BotExternalControlMode::None;
        return;
    }

    float distSq = Helper::DistanceSq(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
        _destinationX, _destinationY, _destinationZ);
    if (distSq <= 2.25f)
    {
        _hasDestination = false;
        _state = BotMovementState::Idle;
        _stoppedTimerMs = 0;
        if (_externalControlMode == BotExternalControlMode::MoveTo)
            _externalControlMode = BotExternalControlMode::None;
    }
    else if (!bot->movespline || bot->movespline->Finalized())
    {
        _stoppedTimerMs += diff;
        if (_stoppedTimerMs >= 1500)
        {
            _hasDestination = false;
            _state = BotMovementState::Idle;
            _stoppedTimerMs = 0;
            if (_externalControlMode == BotExternalControlMode::MoveTo)
                _externalControlMode = BotExternalControlMode::None;
        }
    }
    else
    {
        _stoppedTimerMs = 0;
    }
}

void MovementManager::StopInternal(Player* bot)
{
    if (!bot || bot->HasUnitState(UNIT_STATE_CHARGING))
        return;
    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MoveIdle();
    bot->StopMoving();
    _hasDestination = false;
    _state = BotMovementState::Idle;
    _stoppedTimerMs = 0;
    _waypointCount = 0;
}

bool MovementManager::FollowInternal(Player* bot, Unit* target, float distance, float angle)
{
    if (!bot || !target || bot->HasUnitState(UNIT_STATE_CHARGING))
        return false;
    if (_state == BotMovementState::Following && _target == target->GetGUID() &&
        std::fabs(_followDistance - distance) < 0.01f && std::fabs(_followAngle - angle) < 0.01f)
        return true;

    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MoveFollow(target, distance, ChaseAngle(angle));
    _hasDestination = false;
    _state = BotMovementState::Following;
    _target = target->GetGUID();
    _followDistance = distance;
    _followAngle = angle;
    RecordPosition(bot);
    return true;
}

bool MovementManager::ChaseInternal(Player* bot, Unit* target)
{
    if (!bot || !target || bot->HasUnitState(UNIT_STATE_CHARGING))
        return false;
    if (_state == BotMovementState::Chasing && _target == target->GetGUID())
        return true;

    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MoveChase(target);
    _hasDestination = false;
    _state = BotMovementState::Chasing;
    _target = target->GetGUID();
    RecordPosition(bot);
    return true;
}

bool MovementManager::MoveToInternal(Player* bot, float x, float y, float z, BotMovementState state, bool force)
{
    if (!bot || bot->HasUnitState(UNIT_STATE_CHARGING))
        return false;

    float distSq = Helper::DistanceSq(_requestedX, _requestedY, _requestedZ, x, y, z);
    if (!force && _hasDestination && _state == state && distSq < 4.0f)
        return true;
    if (!force && _pathRetryCooldownMs > 0 && distSq < 4.0f)
        return false;

    _requestedX = x;
    _requestedY = y;
    _requestedZ = z;

    PathGenerator path(bot);
    path.SetUseStraightPath(true);
    bool pathCalculated = path.CalculatePath(x, y, z);
    uint32_t pathFlags = static_cast<uint32_t>(path.GetPathType());
    bool notUsingNavmesh = (pathFlags & PATHFIND_NOT_USING_PATH) != 0;
    bool unsafePath = (pathFlags & (PATHFIND_NOPATH | PATHFIND_SHORTCUT | PATHFIND_SHORT)) != 0;

    if (pathCalculated && !notUsingNavmesh && !unsafePath && path.GetPath().size() >= 2)
    {
        std::vector<Position> pathPoints;
        pathPoints.reserve(path.GetPath().size());
        for (const G3D::Vector3& point : path.GetPath())
        {
            Position position;
            position.Relocate(point.x, point.y, point.z);
            pathPoints.push_back(position);
        }

        const Position& pathEnd = pathPoints.back();
        _destinationX = pathEnd.GetPositionX();
        _destinationY = pathEnd.GetPositionY();
        _destinationZ = pathEnd.GetPositionZ();
        _hasDestination = true;
        _target.Clear();
        _state = state;
        _stoppedTimerMs = 0;
        _waypointCount = pathPoints.size();
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveSmoothPath(WaypointId, pathPoints.data(), pathPoints.size(), false);
        RecordPosition(bot);
        return true;
    }

    // PATHFIND_NOT_USING_PATH is a straight shortcut, not a ground-safe route.
    // Starting a point spline here lets non-flying players interpolate through
    // terrain or through the air when an MMAP tile is unavailable. Treat it as
    // a failed movement request and let the action choose another destination.
    _hasDestination = false;
    _state = BotMovementState::Idle;
    _waypointCount = 0;
    _pathRetryCooldownMs = 1000;
    return false;
}

void MovementManager::RecordPosition(Player* bot)
{
    if (!bot)
        return;
    _lastX = bot->GetPositionX();
    _lastY = bot->GetPositionY();
    _lastZ = bot->GetPositionZ();
}
