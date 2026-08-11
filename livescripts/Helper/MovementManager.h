#pragma once

#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <cstddef>

class Player;
class Unit;

enum class BotMovementState : uint8 { Idle, Moving, Following, Chasing, Fleeing, Stuck };
enum class BotExternalControlMode : uint8 { None, MoveTo, Follow, Chase };

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
    size_t GetWaypointIndex() const { return 0; }
    size_t GetWaypointCount() const { return _waypointCount; }
    float GetDestinationX() const { return _requestedX; }
    float GetDestinationY() const { return _requestedY; }
    float GetDestinationZ() const { return _requestedZ; }

    void Stop();
    void StopExternal();
    void Shutdown();
    void Follow(Unit* target, float distance, float angle);
    void FollowExternal(Unit* target, float distance, float angle);
    void Chase(Unit* target);
    void ChaseExternal(Unit* target);
    bool MoveTo(float x, float y, float z, BotMovementState state = BotMovementState::Moving, bool force = false);
    bool MoveToExternal(float x, float y, float z, BotMovementState state = BotMovementState::Moving, bool force = false);
    void Update(uint32 diff);

private:
    Player* ResolveBot() const;
    void StopInternal(Player* bot);
    bool FollowInternal(Player* bot, Unit* target, float distance, float angle);
    bool ChaseInternal(Player* bot, Unit* target);
    bool MoveToInternal(Player* bot, float x, float y, float z, BotMovementState state, bool force);
    void RecordPosition(Player* bot);

    static constexpr uint32 WaypointId = 0x50424D;

    ObjectGuid _botGuid;
    BotMovementState _state = BotMovementState::Idle;
    ObjectGuid _target;
    float _destinationX = 0.0f, _destinationY = 0.0f, _destinationZ = 0.0f;
    float _requestedX = 0.0f, _requestedY = 0.0f, _requestedZ = 0.0f;
    float _followDistance = 0.0f, _followAngle = 0.0f;
    float _lastX = 0.0f, _lastY = 0.0f, _lastZ = 0.0f;
    uint32 _stoppedTimerMs = 0;
    uint32 _pathRetryCooldownMs = 0;
    size_t _waypointCount = 0;
    bool _hasDestination = false;
    BotExternalControlMode _externalControlMode = BotExternalControlMode::None;
};
