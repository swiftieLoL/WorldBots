#pragma once

#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "Helper/OriginPathRecoveryPolicy.h"
#include <cstddef>
#include <string>
#include <vector>

class Player;
class Unit;
class PathGenerator;

enum class BotMovementState : uint8 { Idle, Moving, Following, Chasing, Fleeing, Stuck };
enum class BotExternalControlMode : uint8 { None, MoveTo, Follow, Chase };
enum class BotPathFailure : uint8 { None, CorridorRejected, CorridorSamplingFailed, GroundPathRejected };

class MovementManager
{
public:
    explicit MovementManager(Player* bot);
    ~MovementManager();

    BotMovementState GetState() const { return _state; }
    const char* GetStateName() const;
    bool HasPath() const;
    bool IsExternallyControlled() const { return _externalControlMode != BotExternalControlMode::None; }
    BotExternalControlMode GetExternalControlMode() const { return _externalControlMode; }
    const char* GetExternalControlModeName() const;
    float GetDestinationX() const { return _requestedX; }
    float GetDestinationY() const { return _requestedY; }
    float GetDestinationZ() const { return _requestedZ; }
    BotPathFailure GetLastPathFailure() const { return _lastPathFailure; }
    const char* GetLastPathFailureName() const;
    uint32 GetLastPathFlags() const { return _lastPathFlags; }
    uint64 GetPathAttemptGeneration() const { return _pathAttemptGeneration; }
    bool NeedsOriginPathRecovery() const
    {
        return _originPathFailures.IsRecoveryRequired() ||
            _disconnectedOriginFailures.IsRecoveryRequired();
    }
    uint32 GetOriginPathFailureCount() const
    {
        return _originPathFailures.GetFailureCount() +
            _disconnectedOriginFailures.GetFailureCount();
    }
    uint32 GetOriginPathFailureDestinationCount() const
    {
        return _originPathFailures.GetDistinctDestinationCount() +
            _disconnectedOriginFailures.GetDistinctDestinationCount();
    }
    void ResetOriginPathRecovery();
    bool GetActivePathEndpoint(float& x, float& y, float& z) const;
    bool GetLastPathAttemptEndpoint(float& x, float& y, float& z) const;
    void SetDiagnosticContext(const char* goal, const char* action,
        uint64 actionInstance, uint32 questId);
    void SetDiagnosticPathSource(std::string source);

    void Stop();
    void StopExternal();
    void Shutdown();
    void Follow(Unit* target, float distance, float angle);
    void FollowExternal(Unit* target, float distance, float angle);
    void FollowExternal(ObjectGuid targetGuid, float distance, float angle);
    void Chase(Unit* target);
    void ChaseExternal(Unit* target);
    void ChaseExternal(ObjectGuid targetGuid);
    void BeginFleeRecovery();
    void EndFleeRecovery();
    bool MoveAwayFrom(Unit* threat, float preferredDistance);
    bool MoveTo(float x, float y, float z, BotMovementState state = BotMovementState::Moving, bool force = false);
    // World travel deliberately returns control to its planner after each
    // short corridor leg so nearby danger can be sensed before continuing.
    bool MoveToTravel(float x, float y, float z, BotMovementState state = BotMovementState::Moving, bool force = false);
    bool MoveToExternal(float x, float y, float z, BotMovementState state = BotMovementState::Moving, bool force = false);
    void Update(uint32 diff);

private:
    Player* ResolveBot() const;
    void StopInternal(Player* bot);
    bool FollowInternal(Player* bot, Unit* target, float distance, float angle);
    bool ChaseInternal(Player* bot, Unit* target);
    bool MoveToInternal(Player* bot, float x, float y, float z,
        BotMovementState state, bool force, bool autoContinueSegments,
        float maximumSegmentDistance);
    void RecordValidatedGroundOrigin(Player* bot);
    bool ValidateCompletedGroundLeg(Player* bot);
    void RecordBreadcrumb(Player* bot, bool force = false);
    bool CommitAndLaunchGroundPath(Player* bot, const PathGenerator& groundPath,
        BotMovementState state, uint32_t pathFlags);

    struct MovementBreadcrumb
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    static constexpr uint32 WaypointId = 0x50424D;

    ObjectGuid _botGuid;
    BotMovementState _state = BotMovementState::Idle;
    ObjectGuid _target;
    float _destinationX = 0.0f, _destinationY = 0.0f, _destinationZ = 0.0f;
    float _requestedX = 0.0f, _requestedY = 0.0f, _requestedZ = 0.0f;
    float _followDistance = 0.0f, _followAngle = 0.0f;
    uint32 _stoppedTimerMs = 0;
    uint32 _pathRetryCooldownMs = 0;
    size_t _waypointCount = 0;
    bool _hasDestination = false;
    BotPathFailure _lastPathFailure = BotPathFailure::None;
    uint32 _lastPathFlags = 0;
    uint64 _pathAttemptGeneration = 0;
    Helper::OriginPathRecoveryPolicy::Tracker _originPathFailures;
    Helper::OriginPathRecoveryPolicy::DisconnectedTracker
        _disconnectedOriginFailures;
    float _lastPathEndpointX = 0.0f;
    float _lastPathEndpointY = 0.0f;
    float _lastPathEndpointZ = 0.0f;
    bool _hasLastPathEndpoint = false;
    uint32 _validatedOriginMapId = 0;
    float _validatedOriginX = 0.0f;
    float _validatedOriginY = 0.0f;
    float _validatedOriginZ = 0.0f;
    bool _hasValidatedOrigin = false;
    BotExternalControlMode _externalControlMode = BotExternalControlMode::None;
    bool _autoContinueSegments = true;
    float _maximumSegmentDistance = 180.0f;
    std::vector<MovementBreadcrumb> _breadcrumbs;
    uint32 _breadcrumbMapId = 0;
    std::string _diagnosticGoal;
    std::string _diagnosticAction;
    uint64 _diagnosticActionInstance = 0;
    uint32 _diagnosticQuestId = 0;
    std::string _diagnosticPathSource;
    bool _hasBreadcrumbMap = false;
    bool _hasFleeRecoveryAnchor = false;
    float _fleeRecoveryX = 0.0f;
    float _fleeRecoveryY = 0.0f;
    float _fleeRecoveryZ = 0.0f;
};
