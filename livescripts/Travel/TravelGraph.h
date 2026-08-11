#pragma once

#include "Helper/CommonTypes.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Travel
{
    enum class TravelMode : uint8_t
    {
        Walk = 0,
        FlightPath,
        Transport,
        Portal,
        Hearthstone
    };

    enum class TravelNodeKind : uint8_t
    {
        Waypoint = 0,
        FlightMaster,
        TransportStop,
        Portal,
        PortalDestination,
        Home
    };

    struct TravelNode
    {
        uint32_t id = 0;
        Common::PositionInfo position;
        TravelNodeKind kind = TravelNodeKind::Waypoint;
        uint32_t reference = 0;
    };

    struct TravelEdge
    {
        uint32_t from = 0;
        uint32_t to = 0;
        TravelMode mode = TravelMode::Walk;
        float cost = 0.0f;
        uint32_t reference = 0;
        uint64_t key = 0;
    };

    struct RouteStep
    {
        TravelMode mode = TravelMode::Walk;
        Common::PositionInfo from;
        Common::PositionInfo to;
        uint32_t fromNodeId = 0;
        uint32_t toNodeId = 0;
        uint32_t fromReference = 0;
        uint32_t toReference = 0;
        uint32_t reference = 0;
        uint64_t edgeKey = 0;
    };

    struct RouteOptions
    {
        std::unordered_set<uint32_t> knownTaxiNodes;
        std::unordered_set<uint64_t> blockedEdges;
        bool canUseHearthstone = false;
        Common::PositionInfo home;
    };

    class TravelGraph
    {
    public:
        uint32_t AddNode(Common::PositionInfo position, TravelNodeKind kind, uint32_t reference = 0);
        uint64_t AddEdge(uint32_t from, uint32_t to, TravelMode mode, float cost, uint32_t reference = 0);
        void AddLocalWalkConnections(std::size_t neighboursPerNode = 6);

        std::vector<RouteStep> FindRoute(Common::PositionInfo start,
            Common::PositionInfo destination, const RouteOptions& options) const;

        const std::vector<TravelNode>& GetNodes() const { return _nodes; }
        const std::vector<TravelEdge>& GetEdges() const { return _edges; }
        const TravelNode* GetNode(uint32_t id) const;

        static float Distance(const Common::PositionInfo& left, const Common::PositionInfo& right);
        static uint64_t MakeEdgeKey(uint32_t from, uint32_t to, TravelMode mode, uint32_t reference);

    private:
        std::vector<TravelNode> _nodes;
        std::vector<TravelEdge> _edges;
    };
}
