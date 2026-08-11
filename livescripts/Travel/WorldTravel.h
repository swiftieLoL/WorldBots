#pragma once

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

    class WorldTravel
    {
    public:
        TravelResult Update(Player* bot, MovementManager* movement,
            Common::PositionInfo destination, uint32_t deltaMs);
        void Stop(Player* bot, MovementManager* movement);
        void Reset();

        bool IsActive() const { return _active; }
        bool IsLongTravel() const { return _active && !_route.empty(); }
        const std::string& GetFailureReason() const { return _failureReason; }
        const char* GetCurrentModeName() const;

        static bool NeedsTravel(Player* bot, const Common::PositionInfo& destination,
            float sameMapDistance = 650.0f);

    private:
        bool BuildRoute(Player* bot);
        TravelResult UpdateStep(Player* bot, MovementManager* movement, uint32_t deltaMs);
        void CompleteStep(Player* bot);
        void FailStep(Player* bot, MovementManager* movement, std::string reason);
        void RecordProgress(Player* bot, uint32_t deltaMs);

        Common::PositionInfo _destination;
        std::vector<RouteStep> _route;
        std::size_t _stepIndex = 0;
        std::unordered_set<uint64_t> _blockedEdges;
        uint32_t _stepElapsedMs = 0;
        uint32_t _noProgressMs = 0;
        uint32_t _replanCount = 0;
        uint32_t _lastMapId = 0;
        float _lastX = 0.0f;
        float _lastY = 0.0f;
        float _lastZ = 0.0f;
        bool _hasProgressPosition = false;
        bool _transitionStarted = false;
        bool _mountAttempted = false;
        bool _active = false;
        bool _failed = false;
        std::string _failureReason;
    };
}
