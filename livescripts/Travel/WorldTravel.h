#pragma once

#include "Brain/SuppressionRegistry.h"
#include "Helper/MovementPathPolicy.h"
#include "ObjectGuid.h"
#include "TravelGraph.h"

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

class MovementManager;
class Player;

namespace Travel
{
    enum class TravelResult : uint8_t
    {
        InProgress = 0,
        Arrived,
        Failed
    };

    enum class TravelWaitReason : uint8_t
    {
        None = 0,
        MountCast,
        Casting,
        PathRequest,
        Walking,
        Flight,
        FlightMaster,
        Transport,
        Portal,
        Hearthstone
    };

    enum class TravelPreflightStatus : uint8_t
    {
        Ready = 0,
        Deferred,
        Unreachable
    };

    struct TravelPreflightResult
    {
        TravelPreflightStatus status = TravelPreflightStatus::Unreachable;
        std::string reason;

        bool CanCommit() const
        {
            return status != TravelPreflightStatus::Unreachable;
        }
    };

    class WorldTravel
    {
    public:
        TravelResult Update(Player* bot, MovementManager* movement,
            Common::PositionInfo destination, uint32_t deltaMs,
            const std::vector<Brain::DangerArea>& dangerAreas = {},
            const std::vector<ObjectGuid>& nearbyHostileGuids = {});
        void Stop(Player* bot, MovementManager* movement);
        void Reset();

        bool IsActive() const { return _active; }
        bool IsLongTravel() const { return _active && !_route.empty(); }
        const std::string& GetFailureReason() const { return _failureReason; }
        bool GetFailureArea(Brain::DangerArea& area) const;
        const char* GetCurrentModeName() const;
        const char* GetWaitReasonName() const;
        uint32_t GetElapsedMs() const { return _elapsedMs; }
        uint32_t GetStepElapsedMs() const { return _stepElapsedMs; }
        uint32_t GetReplanCount() const { return _replanCount; }
        uint32_t GetStepIndex() const { return static_cast<uint32_t>(_stepIndex); }
        uint32_t GetStepCount() const { return static_cast<uint32_t>(_route.size()); }
        uint32_t GetHearthstoneFailureCount() const { return _hearthstoneFailureCount; }
        bool GetDestination(Common::PositionInfo& destination) const
        {
            if (!_hasDestination)
                return false;
            destination = _destination;
            return true;
        }

        static bool NeedsTravel(Player* bot, const Common::PositionInfo& destination,
            float sameMapDistance = 650.0f);
        static bool CanReach(Player* bot, const Common::PositionInfo& destination);
        static TravelPreflightResult Preflight(Player* bot,
            const Common::PositionInfo& destination,
            const std::vector<Brain::DangerArea>& dangerAreas = {},
            const std::vector<ObjectGuid>& nearbyHostileGuids = {});

    private:
        bool BuildRoute(Player* bot);
        TravelResult UpdateStep(Player* bot, MovementManager* movement, uint32_t deltaMs,
            const std::vector<Brain::DangerArea>& dangerAreas,
            const std::vector<ObjectGuid>& nearbyHostileGuids);
        void CompleteStep(Player* bot);
        void FailStep(Player* bot, MovementManager* movement, std::string reason);
        void RecordProgress(Player* bot, const Common::PositionInfo& waypoint,
            uint32_t deltaMs);

        Common::PositionInfo _destination;
        std::vector<RouteStep> _route;
        std::size_t _stepIndex = 0;
        std::unordered_set<uint64_t> _blockedEdges;
        std::unordered_set<uint32_t> _blockedNodes;
        uint32_t _stepElapsedMs = 0;
        uint32_t _elapsedMs = 0;
        uint32_t _replanCount = 0;
        uint32_t _walkPrePathWaitMs = 0;
        uint32_t _walkProbeCooldownMs = 0;
        std::size_t _walkCandidateCursor = 0;
        uint64_t _stepPathAttemptGeneration = 0;
        Helper::MovementPathPolicy::TravelProgressTracker _walkProgress;
        std::vector<Helper::MovementPathPolicy::Point> _visitedWalkFrontiers;
        TravelWaitReason _waitReason = TravelWaitReason::None;
        bool _stepPathBaselineSet = false;
        bool _transitionStarted = false;
        bool _mountAttempted = false;
        bool _hearthstoneBlocked = false;
        bool _hasDestination = false;
        bool _active = false;
        bool _failed = false;
        bool _hasFailureArea = false;
        Brain::DangerArea _failureArea;
        std::string _failureReason;
        uint32_t _hearthstoneFailureCount = 0;
    };
}
