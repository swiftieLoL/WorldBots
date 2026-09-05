#include "MovementManager.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Helper/MovementPathPolicy.h"
#include "Helper/TeleportUtils.h"
#include "Helper/TimeUtils.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    // Use the full straight corridor only to choose a bounded destination on
    // the same route. A second, normal PathGenerator query owns the executable
    // ground path and its heights.
    constexpr float MaximumCorridorSegmentDistance = 180.0f;
    constexpr float MaximumTravelLegDistance = 35.0f;

    static_assert(Helper::MovementPathPolicy::Normal == static_cast<uint32_t>(PATHFIND_NORMAL));
    static_assert(Helper::MovementPathPolicy::Shortcut == static_cast<uint32_t>(PATHFIND_SHORTCUT));
    static_assert(Helper::MovementPathPolicy::Incomplete == static_cast<uint32_t>(PATHFIND_INCOMPLETE));
    static_assert(Helper::MovementPathPolicy::NoPath == static_cast<uint32_t>(PATHFIND_NOPATH));
    static_assert(Helper::MovementPathPolicy::NotUsingPath == static_cast<uint32_t>(PATHFIND_NOT_USING_PATH));
    static_assert(Helper::MovementPathPolicy::Short == static_cast<uint32_t>(PATHFIND_SHORT));
    static_assert(Helper::MovementPathPolicy::FarFromPolyStart == static_cast<uint32_t>(PATHFIND_FARFROMPOLY_START));
    static_assert(Helper::MovementPathPolicy::FarFromPolyEnd == static_cast<uint32_t>(PATHFIND_FARFROMPOLY_END));

    void LaunchGroundPath(Player* bot, const std::vector<Position>& path,
        uint32_t pointId)
    {
        Movement::PointsArray splinePath;
        splinePath.reserve(path.size());
        for (const Position& point : path)
        {
            splinePath.emplace_back(point.GetPositionX(), point.GetPositionY(),
                point.GetPositionZ());
        }

        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->LaunchMoveSpline(
            [splinePath = std::move(splinePath)](
                Movement::MoveSplineInit& init) {
                // PathGenerator already supplies normalized ground-following
                // points. Catmull-Rom smoothing can overshoot those heights
                // between samples and visibly send a player through floors.
                init.MovebyPath(splinePath);
                init.SetWalk(false);
            }, pointId);
    }
}

MovementManager::MovementManager(Player* bot)
{
    if (bot)
    {
        _botGuid = bot->GetGUID();
        RecordBreadcrumb(bot, true);
    }
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

const char* MovementManager::GetLastPathFailureName() const
{
    switch (_lastPathFailure)
    {
        case BotPathFailure::None: return "none";
        case BotPathFailure::CorridorRejected: return "navmesh corridor rejected";
        case BotPathFailure::CorridorSamplingFailed: return "navmesh corridor segment selection failed";
        case BotPathFailure::GroundPathRejected: return "smooth ground path rejected";
        default: return "unknown path failure";
    }
}

void MovementManager::SetDiagnosticContext(const char* goal, const char* action,
    uint64 actionInstance, uint32 questId)
{
    _diagnosticGoal = goal ? goal : "";
    _diagnosticAction = action ? action : "";
    _diagnosticActionInstance = actionInstance;
    _diagnosticQuestId = questId;
    _diagnosticPathSource.clear();
}

void MovementManager::SetDiagnosticPathSource(std::string source)
{
    _diagnosticPathSource = std::move(source);
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
    if (target)
        FollowExternal(target->GetGUID(), distance, angle);
}

void MovementManager::FollowExternal(ObjectGuid targetGuid, float distance, float angle)
{
    Player* bot = ResolveBot();
    Unit* target = (bot && targetGuid) ? ObjectAccessor::GetUnit(*bot, targetGuid) : nullptr;
    if (bot && target && target->IsInWorld() && target->GetMap() == bot->GetMap() &&
        FollowInternal(bot, target, distance, angle))
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
    if (target)
        ChaseExternal(target->GetGUID());
}

void MovementManager::ChaseExternal(ObjectGuid targetGuid)
{
    Player* bot = ResolveBot();
    Unit* target = (bot && targetGuid) ? ObjectAccessor::GetUnit(*bot, targetGuid) : nullptr;
    if (bot && target && target->IsInWorld() && target->GetMap() == bot->GetMap() &&
        ChaseInternal(bot, target))
        _externalControlMode = BotExternalControlMode::Chase;
}

void MovementManager::BeginFleeRecovery()
{
    Player* bot = ResolveBot();
    _hasFleeRecoveryAnchor = false;
    if (!bot || !bot->IsInWorld())
        return;

    std::vector<Helper::MovementPathPolicy::Point> breadcrumbs;
    breadcrumbs.reserve(_breadcrumbs.size());
    for (const MovementBreadcrumb& breadcrumb : _breadcrumbs)
        breadcrumbs.push_back({ breadcrumb.x, breadcrumb.y, breadcrumb.z });

    Helper::MovementPathPolicy::RecoveryAnchor anchor =
        Helper::MovementPathPolicy::SelectRecoveryBreadcrumb(
            breadcrumbs,
            { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() },
            Constants::DefaultFleeStepDistance);
    if (!anchor.valid)
        return;

    _fleeRecoveryX = anchor.point.x;
    _fleeRecoveryY = anchor.point.y;
    _fleeRecoveryZ = anchor.point.z;
    _hasFleeRecoveryAnchor = true;
}

void MovementManager::EndFleeRecovery()
{
    _hasFleeRecoveryAnchor = false;

    // Do not let a later encounter reuse the pre-flee trail from the other
    // side of the escape. Seed a fresh trail at the position that was actually
    // reached.
    _breadcrumbs.clear();
    _hasBreadcrumbMap = false;
    if (Player* bot = ResolveBot())
        RecordBreadcrumb(bot, true);
}

bool MovementManager::MoveAwayFrom(Unit* threat, float preferredDistance)
{
    Player* bot = ResolveBot();
    if (!bot || !threat || !threat->IsInWorld() || threat->GetMap() != bot->GetMap() ||
        IsExternallyControlled() || bot->HasUnitState(UNIT_STATE_CHARGING) ||
        preferredDistance <= 0.0f)
    {
        return false;
    }

    // Probe a fan of possible escape bearings on the navmesh. The shorter
    // fallback radii help in tight spaces without allowing an incomplete path
    // to pull the bot repeatedly toward an unreachable hillside.
    constexpr std::array<float, 9> AngleOffsets = {
        0.0f,
        0.5235988f, -0.5235988f,
        1.0471976f, -1.0471976f,
        1.5707963f, -1.5707963f,
        2.0943951f, -2.0943951f
    };
    constexpr std::array<float, 3> DistanceScales = { 1.0f, 0.67f, 0.4f };
    constexpr float MinimumSeparationGain = 0.75f;
    constexpr float AllowedTemporaryApproach = 1.0f;

    float botX = bot->GetPositionX();
    float botY = bot->GetPositionY();
    float botZ = bot->GetPositionZ();
    float threatX = threat->GetPositionX();
    float threatY = threat->GetPositionY();
    float currentDx = botX - threatX;
    float currentDy = botY - threatY;
    float currentSeparation = std::sqrt(currentDx * currentDx + currentDy * currentDy);
    float awayAngle = currentSeparation > 0.01f
        ? std::atan2(currentDy, currentDx)
        : bot->GetOrientation();

    float bestScore = -std::numeric_limits<float>::infinity();
    float bestSeparationGain = 0.0f;
    uint32 bestPathFlags = 0;
    std::vector<Position> bestPath;
    std::vector<Position> recoveryPath;
    uint32 recoveryPathFlags = 0;
    std::vector<Helper::MovementPathPolicy::Point> distinctFullDistanceEndpoints;

    // First validate the frozen pre-combat breadcrumb. It is the only route we
    // trust inside a corridor: the bot has already traversed it successfully,
    // and it prevents a locally valid radial probe from choosing a cave pocket.
    if (_hasFleeRecoveryAnchor)
    {
        PathGenerator path(bot);
        path.SetUseStraightPath(false);
        bool calculated = path.CalculatePath(
            _fleeRecoveryX, _fleeRecoveryY, _fleeRecoveryZ);
        uint32 pathFlags = static_cast<uint32>(path.GetPathType());
        if (Helper::MovementPathPolicy::IsCompleteGroundPath(
            calculated, pathFlags, path.GetPath().size()))
        {
            const auto& points = path.GetPath();
            const G3D::Vector3& endpoint = points.back();
            float endDx = endpoint.x - threatX;
            float endDy = endpoint.y - threatY;
            float endSeparation = std::sqrt(endDx * endDx + endDy * endDy);
            float minimumSeparation = currentSeparation;
            for (const G3D::Vector3& point : points)
            {
                float pointDx = point.x - threatX;
                float pointDy = point.y - threatY;
                minimumSeparation = std::min(minimumSeparation,
                    std::sqrt(pointDx * pointDx + pointDy * pointDy));
            }

            if (endSeparation - currentSeparation >= MinimumSeparationGain &&
                minimumSeparation + AllowedTemporaryApproach >= currentSeparation)
            {
                recoveryPathFlags = pathFlags;
                recoveryPath.reserve(points.size());
                for (const G3D::Vector3& point : points)
                {
                    Position position;
                    position.Relocate(point.x, point.y, point.z);
                    recoveryPath.push_back(position);
                }
            }
        }
    }

    // If the single pre-combat anchor was unavailable or unviable, evaluate
    // the bot's broader historical breadcrumb trail in reverse. Earlier
    // breadcrumbs lead back toward the entrance/exterior of interior spaces.
    if (recoveryPath.empty() && !_breadcrumbs.empty())
    {
        std::vector<Helper::MovementPathPolicy::Point> trail;
        trail.reserve(_breadcrumbs.size());
        for (const MovementBreadcrumb& b : _breadcrumbs)
            trail.push_back({ b.x, b.y, b.z });

        std::vector<Helper::MovementPathPolicy::Point> candidates =
            Helper::MovementPathPolicy::CollectBacktrackBreadcrumbs(
                trail, { botX, botY, botZ }, 4.0f, preferredDistance * 1.5f);

        for (const Helper::MovementPathPolicy::Point& candidate : candidates)
        {
            PathGenerator path(bot);
            path.SetUseStraightPath(false);
            bool calculated = path.CalculatePath(candidate.x, candidate.y, candidate.z);
            uint32 pathFlags = static_cast<uint32>(path.GetPathType());
            if (!Helper::MovementPathPolicy::IsCompleteGroundPath(
                calculated, pathFlags, path.GetPath().size()))
                continue;

            const auto& points = path.GetPath();
            const G3D::Vector3& endpoint = points.back();
            float endDx = endpoint.x - threatX;
            float endDy = endpoint.y - threatY;
            float endSeparation = std::sqrt(endDx * endDx + endDy * endDy);
            float minimumSeparation = currentSeparation;
            for (const G3D::Vector3& point : points)
            {
                float pointDx = point.x - threatX;
                float pointDy = point.y - threatY;
                minimumSeparation = std::min(minimumSeparation,
                    std::sqrt(pointDx * pointDx + pointDy * pointDy));
            }

            if (endSeparation - currentSeparation >= MinimumSeparationGain &&
                minimumSeparation + AllowedTemporaryApproach >= currentSeparation)
            {
                recoveryPathFlags = pathFlags;
                recoveryPath.clear();
                recoveryPath.reserve(points.size());
                for (const G3D::Vector3& point : points)
                {
                    Position position;
                    position.Relocate(point.x, point.y, point.z);
                    recoveryPath.push_back(position);
                }
                break;
            }
        }
    }

    if (!recoveryPath.empty())
    {
        bestPath = std::move(recoveryPath);
        bestPathFlags = recoveryPathFlags;
    }
    else
    {
        for (std::size_t distanceIndex = 0;
            distanceIndex < DistanceScales.size(); ++distanceIndex)
        {
            float distanceScale = DistanceScales[distanceIndex];
            float candidateDistance = preferredDistance * distanceScale;
            for (float angleOffset : AngleOffsets)
            {
                float angle = awayAngle + angleOffset;
                float targetX = botX + std::cos(angle) * candidateDistance;
                float targetY = botY + std::sin(angle) * candidateDistance;

                // Keep the query on the bot's current vertical layer and let
                // PathGenerator resolve the navmesh height. Map::GetHeight can
                // return a tree canopy, roof, or upper terrain layer here.
                PathGenerator path(bot);
                path.SetUseStraightPath(false);
                bool calculated = path.CalculatePath(targetX, targetY, botZ);
                uint32 pathFlags = static_cast<uint32>(path.GetPathType());
                if (!Helper::MovementPathPolicy::IsCompleteGroundPath(
                    calculated, pathFlags, path.GetPath().size()))
                {
                    continue;
                }

                const auto& points = path.GetPath();
                const G3D::Vector3& endpoint = points.back();
                float endDx = endpoint.x - threatX;
                float endDy = endpoint.y - threatY;
                float endSeparation = std::sqrt(endDx * endDx + endDy * endDy);
                float separationGain = endSeparation - currentSeparation;
                if (separationGain < MinimumSeparationGain)
                    continue;

                float routeLength = 0.0f;
                float uphillRise = 0.0f;
                float minimumSeparation = currentSeparation;
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const G3D::Vector3& point = points[i];
                    float pointDx = point.x - threatX;
                    float pointDy = point.y - threatY;
                    minimumSeparation = std::min(minimumSeparation,
                        std::sqrt(pointDx * pointDx + pointDy * pointDy));
                    uphillRise = std::max(uphillRise, point.z - botZ);

                    if (i > 0)
                    {
                        const G3D::Vector3& previous = points[i - 1];
                        float dx = point.x - previous.x;
                        float dy = point.y - previous.y;
                        float dz = point.z - previous.z;
                        routeLength += std::sqrt(dx * dx + dy * dy + dz * dz);
                    }
                }

                if (minimumSeparation + AllowedTemporaryApproach < currentSeparation)
                    continue;

                if (distanceIndex == 0)
                {
                    Helper::MovementPathPolicy::Point endpointPoint{
                        endpoint.x, endpoint.y, endpoint.z
                    };
                    if (Helper::MovementPathPolicy::IsDistinctEndpoint(
                        distinctFullDistanceEndpoints, endpointPoint))
                    {
                        distinctFullDistanceEndpoints.push_back(endpointPoint);
                    }
                }

                float score = Helper::MovementPathPolicy::ScoreFleeCandidate(
                    separationGain, routeLength, uphillRise);
                if (score <= bestScore)
                    continue;

                bestScore = score;
                bestSeparationGain = separationGain;
                bestPathFlags = pathFlags;
                bestPath.clear();
                bestPath.reserve(points.size());
                for (const G3D::Vector3& point : points)
                {
                    Position position;
                    position.Relocate(point.x, point.y, point.z);
                    bestPath.push_back(position);
                }
            }
        }

        bool confined = Helper::MovementPathPolicy::IsConfinedEscapeSpace(
            distinctFullDistanceEndpoints.size());
        if (confined)
        {
            // In a corridor or pocket, an unproven radial endpoint can be farther
            // from the attacker yet deeper in an unrecoverable cave branch.
            // If the radial escape provides significant separation gain (>= 5.0 yds)
            // away from the threat down a clear tunnel, take it rather than freezing.
            if (!Helper::MovementPathPolicy::ShouldAcceptConfinedRadialEscape(bestSeparationGain))
            {
                bestPath.clear();
            }
        }
    }

    if (bestPath.empty())
    {
        _hasDestination = false;
        _state = BotMovementState::Idle;
        _waypointCount = 0;
        _lastPathFailure = BotPathFailure::GroundPathRejected;
        _lastPathFlags = 0;
        return false;
    }

    const Position& endpoint = bestPath.back();
    _requestedX = endpoint.GetPositionX();
    _requestedY = endpoint.GetPositionY();
    _requestedZ = endpoint.GetPositionZ();
    _destinationX = _requestedX;
    _destinationY = _requestedY;
    _destinationZ = _requestedZ;
    _hasDestination = true;
    _target.Clear();
    _state = BotMovementState::Fleeing;
    _stoppedTimerMs = 0;
    _waypointCount = bestPath.size();
    _lastPathFailure = BotPathFailure::None;
    _lastPathFlags = bestPathFlags;
    _originPathFailures.Reset();
    _disconnectedOriginFailures.Reset();
    RecordValidatedGroundOrigin(bot);
    LaunchGroundPath(bot, bestPath, WaypointId);
    return true;
}

bool MovementManager::MoveTo(float x, float y, float z, BotMovementState state, bool force)
{
    Player* bot = ResolveBot();
    if (!bot || IsExternallyControlled())
        return false;
    return MoveToInternal(bot, x, y, z, state, force, true,
        MaximumCorridorSegmentDistance);
}

bool MovementManager::MoveToTravel(float x, float y, float z,
    BotMovementState state, bool force)
{
    Player* bot = ResolveBot();
    if (!bot || IsExternallyControlled())
        return false;
    return MoveToInternal(bot, x, y, z, state, force, false,
        MaximumTravelLegDistance);
}

bool MovementManager::MoveToExternal(float x, float y, float z, BotMovementState state, bool force)
{
    Player* bot = ResolveBot();
    if (!bot)
        return false;
    bool started = MoveToInternal(bot, x, y, z, state, force, true,
        MaximumCorridorSegmentDistance);
    if (started)
        _externalControlMode = BotExternalControlMode::MoveTo;
    return started;
}

void MovementManager::Update(uint32 diff)
{
    Player* bot = ResolveBot();
    if (!bot)
        return;

    Helper::OriginPathRecoveryPolicy::Point currentPosition{
        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()
    };
    _originPathFailures.RecordMovement(bot->GetMapId(), currentPosition);
    _disconnectedOriginFailures.RecordMovement(bot->GetMapId(), currentPosition);

    if (_state != BotMovementState::Fleeing)
        RecordBreadcrumb(bot);

    _pathRetryCooldownMs = _pathRetryCooldownMs > diff ? _pathRetryCooldownMs - diff : 0;

    // External follow/chase leases store only the target GUID. Resolve it on
    // every tick so despawn, logout, or map changes cannot leave a borrowed
    // Unit pointer in module-owned state.
    if (_externalControlMode == BotExternalControlMode::Follow ||
        _externalControlMode == BotExternalControlMode::Chase)
    {
        Unit* target = _target ? ObjectAccessor::GetUnit(*bot, _target) : nullptr;
        if (!target || !target->IsInWorld() || target->GetMap() != bot->GetMap())
        {
            StopExternal();
            return;
        }
        if (_externalControlMode == BotExternalControlMode::Follow)
            FollowInternal(bot, target, _followDistance, _followAngle);
        else
            ChaseInternal(bot, target);
        return;
    }

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
        BotMovementState continuationState = _state;
        float requestedX = _requestedX;
        float requestedY = _requestedY;
        float requestedZ = _requestedZ;
        float remainingX = _requestedX - _destinationX;
        float remainingY = _requestedY - _destinationY;
        bool continuePath = remainingX * remainingX + remainingY * remainingY > 4.0f;
        if (!ValidateCompletedGroundLeg(bot))
            return;
        _hasDestination = false;
        _state = BotMovementState::Idle;
        _stoppedTimerMs = 0;

        if (continuePath && _autoContinueSegments &&
            MoveToInternal(bot, requestedX, requestedY, requestedZ,
                continuationState, true, _autoContinueSegments,
                _maximumSegmentDistance))
            return;

        if (_externalControlMode == BotExternalControlMode::MoveTo)
            _externalControlMode = BotExternalControlMode::None;
    }
    else if (!bot->movespline || bot->movespline->Finalized())
    {
        _stoppedTimerMs += diff;
        if (_stoppedTimerMs >= Constants::StoppedMovementTimeoutMs)
        {
            if (!ValidateCompletedGroundLeg(bot))
                return;
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
    return true;
}

bool MovementManager::CommitAndLaunchGroundPath(Player* bot,
    const PathGenerator& groundPath,
    BotMovementState state, uint32_t pathFlags)
{
    std::vector<Position> pathPoints;
    pathPoints.reserve(groundPath.GetPath().size());
    for (const G3D::Vector3& point : groundPath.GetPath())
    {
        Position position;
        position.Relocate(point.x, point.y, point.z);
        pathPoints.push_back(position);
    }

    const Position& pathEnd = pathPoints.back();
    _destinationX = pathEnd.GetPositionX();
    _destinationY = pathEnd.GetPositionY();
    _destinationZ = pathEnd.GetPositionZ();
    _lastPathEndpointX = _destinationX;
    _lastPathEndpointY = _destinationY;
    _lastPathEndpointZ = _destinationZ;
    _hasLastPathEndpoint = true;
    _hasDestination = true;
    _target.Clear();
    _state = state;
    _stoppedTimerMs = 0;
    _waypointCount = pathPoints.size();
    _lastPathFailure = BotPathFailure::None;
    _lastPathFlags = pathFlags;
    _originPathFailures.Reset();
    _disconnectedOriginFailures.Reset();
    RecordValidatedGroundOrigin(bot);
    LaunchGroundPath(bot, pathPoints, WaypointId);
    return true;
}

bool MovementManager::MoveToInternal(Player* bot, float x, float y, float z,
    BotMovementState state, bool force, bool autoContinueSegments,
    float maximumSegmentDistance)
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
    _autoContinueSegments = autoContinueSegments;
    _maximumSegmentDistance = maximumSegmentDistance;

    // Identify the path query that produces the flags below. The brain uses
    // this generation to reject flags left behind by an earlier action.
    ++_pathAttemptGeneration;

    // Fast path: for short distances (<= 35 yards, e.g. looting, melee positioning,
    // or town interactions), directly query the smooth ground path. If complete,
    // skip the straight corridor discovery query entirely.
    float directDistSq = Helper::DistanceSq(bot->GetPositionX(), bot->GetPositionY(),
        bot->GetPositionZ(), x, y, z);
    if (directDistSq <= (MaximumTravelLegDistance * MaximumTravelLegDistance))
    {
        PathGenerator directGround(bot);
        directGround.SetUseStraightPath(false);
        bool directCalculated = directGround.CalculatePath(x, y, z);
        uint32_t directFlags = static_cast<uint32_t>(directGround.GetPathType());
        if (Helper::MovementPathPolicy::IsCompleteGroundPath(directCalculated,
            directFlags, directGround.GetPath().size()))
        {
            return CommitAndLaunchGroundPath(bot, directGround, state, directFlags);
        }
    }

    // Obtain a normalized surface route to the requested destination. An
    // incomplete route may guide progressive exploration toward a local
    // frontier, but the route itself is never launched; the selected local leg
    // is revalidated independently below.
    PathGenerator routePath(bot);
    // The discovery route needs corridor corners, not a smooth point every
    // four yards. Smooth paths exhaust Trinity's fixed 74-point budget at
    // roughly 296 yards and falsely reject remote destinations. The selected
    // bounded leg is still re-queried below as a normal smooth ground path.
    routePath.SetUseStraightPath(true);
    bool routeCalculated = routePath.CalculatePath(x, y, z);
    uint32_t routeFlags = static_cast<uint32_t>(routePath.GetPathType());

    std::vector<Helper::MovementPathPolicy::Point> routePoints;
    routePoints.reserve(routePath.GetPath().size());
    for (const G3D::Vector3& point : routePath.GetPath())
        routePoints.push_back({ point.x, point.y, point.z });

    _hasLastPathEndpoint = !routePoints.empty();
    if (_hasLastPathEndpoint)
    {
        const auto& endpoint = routePoints.back();
        _lastPathEndpointX = endpoint.x;
        _lastPathEndpointY = endpoint.y;
        _lastPathEndpointZ = endpoint.z;
    }

    Helper::MovementPathPolicy::CorridorSegment routeSegment;
    bool completeRoute = Helper::MovementPathPolicy::IsCompleteGroundPath(
        routeCalculated, routeFlags, routePoints.size());
    bool normalizedEndpointRoute = !routePoints.empty() &&
        Helper::MovementPathPolicy::IsUsableNormalizedEndpoint(
            routeCalculated, routeFlags, routePoints.size(),
            { x, y, z }, routePoints.back());
    bool routeFrontierUsable =
        Helper::MovementPathPolicy::IsUsableTravelFrontier(
            routeCalculated, routeFlags, routePoints.size());
    bool routeUsable = completeRoute || normalizedEndpointRoute ||
        routeFrontierUsable;
    if (normalizedEndpointRoute)
    {
        // Stop continuation at the surface-normalized endpoint rather than
        // repeatedly requesting the bad authored Z after the final leg.
        _requestedX = routePoints.back().x;
        _requestedY = routePoints.back().y;
        _requestedZ = routePoints.back().z;
    }
    if (routeUsable)
    {
        routeSegment = Helper::MovementPathPolicy::SelectCorridorSegment(
            routePoints, maximumSegmentDistance, true);
    }

    bool groundPathCalculated = false;
    uint32_t groundPathFlags = 0;
    size_t groundPathPointCount = 0;
    if (routeSegment.valid)
    {
        // Re-query the selected local surface point. Even when the discovery
        // route was incomplete, the executable leg must prove a complete path
        // from the bot's current navmesh origin.
        PathGenerator groundPath(bot);
        groundPath.SetUseStraightPath(false);
        groundPathCalculated = groundPath.CalculatePath(
            routeSegment.endpoint.x, routeSegment.endpoint.y,
            routeSegment.endpoint.z);
        groundPathFlags = static_cast<uint32_t>(groundPath.GetPathType());
        groundPathPointCount = groundPath.GetPath().size();

        if (Helper::MovementPathPolicy::IsCompleteGroundPath(groundPathCalculated,
            groundPathFlags, groundPath.GetPath().size()))
        {
            return CommitAndLaunchGroundPath(bot, groundPath, state, groundPathFlags);
        }
    }

    // PATHFIND_NOT_USING_PATH is a straight shortcut, not a ground-safe route.
    // Starting a point spline here lets non-flying players interpolate through
    // terrain or through the air when an MMAP tile is unavailable. Treat it as
    // a failed movement request and let the action choose another destination.
    _hasDestination = false;
    _state = BotMovementState::Idle;
    _waypointCount = 0;
    _lastPathFailure = !routeUsable
        ? BotPathFailure::CorridorRejected
        : (!routeSegment.valid
            ? BotPathFailure::CorridorSamplingFailed
            : BotPathFailure::GroundPathRejected);
    _lastPathFlags = routeSegment.valid ? groundPathFlags : routeFlags;
    if ((_lastPathFlags & Helper::MovementPathPolicy::FarFromPolyStart) != 0)
    {
        _disconnectedOriginFailures.Reset();
        _originPathFailures.Observe(Helper::MonotonicMilliseconds(),
            bot->GetMapId(),
            { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() },
            { x, y, z });
    }
    else
    {
        // A path query whose start attached to the navmesh disproves an older
        // detached-origin window. A fan of nearby attached-start failures is
        // tracked separately as evidence of a disconnected local island.
        _originPathFailures.Reset();
        _disconnectedOriginFailures.Observe(
            Helper::MonotonicMilliseconds(), bot->GetMapId(),
            { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() },
            { x, y, z });
    }
    if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
    {
        Diagnostics::StructuredEvent event;
        event.event = "path_attempt_failed";
        event.goal = _diagnosticGoal;
        event.action = _diagnosticAction;
        event.actionInstance = _diagnosticActionInstance;
        event.questId = _diagnosticQuestId;
        event.requestX = x;
        event.requestY = y;
        event.requestZ = z;
        event.endpointAvailable = _hasLastPathEndpoint;
        event.endpointX = _lastPathEndpointX;
        event.endpointY = _lastPathEndpointY;
        event.endpointZ = _lastPathEndpointZ;
        event.pathFailure = GetLastPathFailureName();
        event.pathFlags = _lastPathFlags;
        event.pathAttemptGeneration = _pathAttemptGeneration;
        event.originFailureCount = GetOriginPathFailureCount();
        event.originDestinationCount =
            GetOriginPathFailureDestinationCount();
        event.originRecoveryRequired = NeedsOriginPathRecovery();
        std::ostringstream details;
        details << "path_source=" << _diagnosticPathSource
            << ";route_calculated=" << routeCalculated
            << ";route_flags=" << routeFlags
            << ";route_points=" << routePoints.size()
            << ";route_complete=" << completeRoute
            << ";route_endpoint_corrected=" << normalizedEndpointRoute
            << ";route_frontier=" << routeFrontierUsable
            << ";segment_valid=" << routeSegment.valid
            << ";ground_calculated=" << groundPathCalculated
            << ";ground_flags=" << groundPathFlags
            << ";ground_points=" << groundPathPointCount
            << ";maximum_segment=" << maximumSegmentDistance
            << ";auto_continue=" << autoContinueSegments;
        event.details = details.str();
        Diagnostics::StructuredEventLog::Write(bot, std::move(event));
    }
    _pathRetryCooldownMs = 1000;
    return false;
}

void MovementManager::RecordValidatedGroundOrigin(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return;

    _validatedOriginMapId = bot->GetMapId();
    _validatedOriginX = bot->GetPositionX();
    _validatedOriginY = bot->GetPositionY();
    _validatedOriginZ = bot->GetPositionZ();
    _hasValidatedOrigin = true;
}

bool MovementManager::ValidateCompletedGroundLeg(Player* bot)
{
    if (!bot || !_hasValidatedOrigin ||
        bot->GetMapId() != _validatedOriginMapId)
    {
        return true;
    }

    // A complete route back to the leg's proven starting point verifies both
    // that the current origin still attaches to the navmesh and that spline
    // execution did not land on another disconnected vertical layer.
    PathGenerator validationPath(bot);
    validationPath.SetUseStraightPath(false);
    bool calculated = validationPath.CalculatePath(
        _validatedOriginX, _validatedOriginY, _validatedOriginZ);
    uint32_t flags = static_cast<uint32_t>(validationPath.GetPathType());
    if (Helper::MovementPathPolicy::IsCompleteGroundPath(
        calculated, flags, validationPath.GetPath().size()))
    {
        RecordBreadcrumb(bot);
        return true;
    }

    // If the backward path failed, check whether the bot is safely standing on valid
    // ground navmesh at its current location. Ledges, one-way drops, or slight slope
    // asymmetries must not trigger an endless backward teleport loop.
    if (Helper::TeleportUtils::HasUsableGroundOrigin(bot))
    {
        RecordValidatedGroundOrigin(bot);
        RecordBreadcrumb(bot);
        return true;
    }

    float failedX = bot->GetPositionX();
    float failedY = bot->GetPositionY();
    float failedZ = bot->GetPositionZ();
    StopInternal(bot);

    constexpr float RecoveryHeightOffset = 0.5f;
    bool restored = false;
    if (bot->GetDistance2d(_validatedOriginX, _validatedOriginY) <= _maximumSegmentDistance * 2.5f)
    {
        bot->NearTeleportTo(_validatedOriginX, _validatedOriginY,
            _validatedOriginZ + RecoveryHeightOffset, bot->GetOrientation());
        restored = Helper::TeleportUtils::CompletePendingTeleport(bot);
    }

    _lastPathFailure = BotPathFailure::CorridorRejected;
    _lastPathFlags = Helper::MovementPathPolicy::FarFromPolyStart;
    _pathRetryCooldownMs = 1000;
    _originPathFailures.Reset();
    _disconnectedOriginFailures.Reset();
    _hasDestination = false;
    _state = BotMovementState::Idle;

    if (restored)
    {
        RecordBreadcrumb(bot, true);
        TC_LOG_WARN("server", "[WorldBots] [Movement] Bot '{}' completed a ground leg outside its validated navmesh corridor at ({:.1f}, {:.1f}, {:.1f}) (flags {}); rolled back to the last verified origin ({:.1f}, {:.1f}, {:.1f})",
            bot->GetName(), failedX, failedY, failedZ, flags,
            _validatedOriginX, _validatedOriginY, _validatedOriginZ);
    }
    else
    {
        TC_LOG_ERROR("server", "[WorldBots] [Movement] Bot '{}' completed a ground leg outside its validated navmesh corridor at ({:.1f}, {:.1f}, {:.1f}) (flags {}) and rollback to ({:.1f}, {:.1f}, {:.1f}) did not complete",
            bot->GetName(), failedX, failedY, failedZ, flags,
            _validatedOriginX, _validatedOriginY, _validatedOriginZ);
    }
    return false;
}

void MovementManager::ResetOriginPathRecovery()
{
    _originPathFailures.Reset();
    _disconnectedOriginFailures.Reset();
}

bool MovementManager::GetActivePathEndpoint(float& x, float& y, float& z) const
{
    if (!_hasDestination)
        return false;
    x = _destinationX;
    y = _destinationY;
    z = _destinationZ;
    return true;
}

bool MovementManager::GetLastPathAttemptEndpoint(float& x, float& y, float& z) const
{
    if (!_hasLastPathEndpoint)
        return false;
    x = _lastPathEndpointX;
    y = _lastPathEndpointY;
    z = _lastPathEndpointZ;
    return true;
}

void MovementManager::RecordBreadcrumb(Player* bot, bool force)
{
    if (!bot || !bot->IsInWorld())
        return;

    uint32 mapId = bot->GetMapId();
    if (!_hasBreadcrumbMap || _breadcrumbMapId != mapId)
    {
        _breadcrumbs.clear();
        _breadcrumbMapId = mapId;
        _hasBreadcrumbMap = true;
        force = true;
    }

    MovementBreadcrumb candidate{
        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()
    };
    if (!force && !_breadcrumbs.empty())
    {
        const MovementBreadcrumb& latest = _breadcrumbs.back();
        float dx = candidate.x - latest.x;
        float dy = candidate.y - latest.y;
        if (dx * dx + dy * dy < 16.0f)
            return;
    }

    _breadcrumbs.push_back(candidate);
    constexpr std::size_t MaximumBreadcrumbs = 16;
    if (_breadcrumbs.size() > MaximumBreadcrumbs)
        _breadcrumbs.erase(_breadcrumbs.begin());
}
