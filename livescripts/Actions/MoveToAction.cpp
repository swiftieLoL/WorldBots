#include "MoveToAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Helper/MathUtils.h"
#include <limits>

namespace Actions
{
    MoveToAction::MoveToAction(float x, float y, float z, uint32_t mapId)
        : _x(x), _y(y), _z(z), _mapId(mapId), _started(false)
    {
    }

    void MoveToAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        _started = false;
        _pathAttempts = 0;
        _retryElapsedMs = 0;
        if (_mapId != 0 && (!bot || bot->GetMapId() != _mapId))
        {
            Finish(ActionOutcome::Blocked, "destination is on another map",
                FailureCategory::Navigation, RecoveryDirective::Replan);
            return;
        }

        if (movement)
        {
            TryMove(movement, true);
        }
        else
        {
            Finish(ActionOutcome::RetryableFailure, "movement manager was unavailable",
                FailureCategory::Navigation, RecoveryDirective::RetryLater);
        }
    }

    void MoveToAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t deltaMs)
    {
        if (!movement || !bot)
        {
            Finish(ActionOutcome::RetryableFailure, "movement context became unavailable",
                FailureCategory::Navigation, RecoveryDirective::RetryLater);
            return;
        }

        if (_mapId != 0 && bot->GetMapId() != _mapId)
        {
            Finish(ActionOutcome::Blocked, "destination map changed during movement",
                FailureCategory::Navigation, RecoveryDirective::Replan);
            return;
        }

        if (!_started)
        {
            RetryMove(movement, deltaMs);
        }
        else if (movement->GetState() == BotMovementState::Idle)
        {
            if (Helper::IsIn3DRange(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), _x, _y, _z, 3.0f))
            {
                Finish(ActionOutcome::Succeeded, "destination reached");
            }
            else
            {
                _started = false;
                RetryMove(movement, deltaMs);
            }
        }
        else
        {
            _retryElapsedMs = 0;
        }
    }

    bool MoveToAction::TryMove(MovementManager* movement, bool force)
    {
        uint64_t generationBefore = movement->GetPathAttemptGeneration();
        bool started = movement->MoveTo(
            _x, _y, _z, BotMovementState::Moving, force);
        uint64_t generationAfter = movement->GetPathAttemptGeneration();
        if (generationAfter != generationBefore)
            ++_pathAttempts;

        _started = started;
        if (!started && generationAfter != generationBefore &&
            _pathAttempts >= MaxPathAttempts)
        {
            Finish(ActionOutcome::RetryableFailure,
                std::string("destination path failed after ") +
                    std::to_string(_pathAttempts) + " attempts (" +
                    movement->GetLastPathFailureName() + ")",
                FailureCategory::Navigation, RecoveryDirective::Replan);
        }
        return started;
    }

    void MoveToAction::RetryMove(MovementManager* movement, uint32_t deltaMs)
    {
        _retryElapsedMs = deltaMs >
                std::numeric_limits<uint32_t>::max() - _retryElapsedMs
            ? std::numeric_limits<uint32_t>::max()
            : _retryElapsedMs + deltaMs;

        if (_pathAttempts >= MaxPathAttempts ||
            _retryElapsedMs >= RetryTimeoutMs)
        {
            Finish(ActionOutcome::RetryableFailure,
                _pathAttempts >= MaxPathAttempts
                    ? "destination remained unreachable after bounded path retries"
                    : "destination path retry timed out",
                FailureCategory::Navigation, RecoveryDirective::Replan);
            return;
        }

        TryMove(movement, false);
    }

    void MoveToAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
        {
            movement->Stop();
        }
    }
}
