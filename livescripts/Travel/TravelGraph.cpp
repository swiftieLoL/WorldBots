#include "TravelGraph.h"
#include "Helper/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace Travel
{
    namespace
    {
        constexpr uint32_t StartNodeId = 0xFFFFFFFEu;
        constexpr uint32_t DestinationNodeId = 0xFFFFFFFDu;
        constexpr uint32_t HomeNodeId = 0xFFFFFFFCu;
        constexpr float DirectWalkDistance = 650.0f;
        constexpr std::size_t ConnectorCount = 6;

        bool CanDepartByWalking(TravelNodeKind kind)
        {
            // Reaching a portal source commits the route to its explicit
            // portal edge. It must not become a cheap walk-through waypoint.
            return kind != TravelNodeKind::Portal;
        }

        bool CanArriveByWalking(TravelNodeKind kind)
        {
            // A portal destination is a landing point, not a place a bot can
            // assume is reachable from an adjacent room or outdoor node.
            return kind != TravelNodeKind::PortalDestination;
        }

        struct QueueEntry
        {
            float cost;
            uint32_t index;

            bool operator>(const QueueEntry& other) const { return cost > other.cost; }
        };
    }

    uint32_t TravelGraph::AddNode(Common::PositionInfo position, TravelNodeKind kind, uint32_t reference)
    {
        uint32_t id = static_cast<uint32_t>(_nodes.size());
        _nodes.push_back({ id, position, kind, reference });
        return id;
    }

    uint64_t TravelGraph::MakeEdgeKey(uint32_t from, uint32_t to, TravelMode mode, uint32_t reference)
    {
        uint64_t key = Helper::HashUtils::FNV1aBasis;
        Helper::HashUtils::MixHash(key, from);
        Helper::HashUtils::MixHash(key, to);
        Helper::HashUtils::MixHash(key, static_cast<uint8_t>(mode));
        Helper::HashUtils::MixHash(key, reference);
        return key;
    }

    uint64_t TravelGraph::AddEdge(uint32_t from, uint32_t to, TravelMode mode, float cost, uint32_t reference)
    {
        if (from >= _nodes.size() || to >= _nodes.size() || from == to)
            return 0;

        uint64_t key = MakeEdgeKey(from, to, mode, reference);
        auto duplicate = std::find_if(_edges.begin(), _edges.end(), [key](const TravelEdge& edge) {
            return edge.key == key;
        });
        if (duplicate == _edges.end())
            _edges.push_back({ from, to, mode, std::max(1.0f, cost), reference, key });
        return key;
    }

    float TravelGraph::Distance(const Common::PositionInfo& left, const Common::PositionInfo& right)
    {
        if (left.mapId != right.mapId)
            return std::numeric_limits<float>::max();
        return Helper::Distance3D(left.x, left.y, left.z, right.x, right.y, right.z);
    }

    bool TravelGraph::IsGlobalPortalTransition(const Common::PositionInfo& source,
        const Common::PositionInfo& destination)
    {
        // Local teleports such as the Stormwind Mage Tower entrance and exit
        // are interior navigation, not world-travel shortcuts. Treating them
        // as graph anchors can strand a bot on an isolated navmesh island.
        return source.mapId != destination.mapId;
    }

    const TravelNode* TravelGraph::GetNode(uint32_t id) const
    {
        return id < _nodes.size() ? &_nodes[id] : nullptr;
    }

    void TravelGraph::AddLocalWalkConnections(std::size_t neighboursPerNode)
    {
        for (const TravelNode& from : _nodes)
        {
            if (!CanDepartByWalking(from.kind))
                continue;

            std::vector<std::pair<float, uint32_t>> nearest;
            nearest.reserve(_nodes.size());
            for (const TravelNode& to : _nodes)
            {
                if (from.id == to.id || from.position.mapId != to.position.mapId ||
                    !CanArriveByWalking(to.kind))
                    continue;
                float distance = Distance(from.position, to.position);
                if (std::isfinite(distance))
                    nearest.emplace_back(distance, to.id);
            }

            std::sort(nearest.begin(), nearest.end());
            if (nearest.size() > neighboursPerNode)
                nearest.resize(neighboursPerNode);
            for (const auto& [distance, to] : nearest)
                AddEdge(from.id, to, TravelMode::Walk, distance);
        }
    }

    std::vector<RouteStep> TravelGraph::FindRoute(Common::PositionInfo start,
        Common::PositionInfo destination, const RouteOptions& options) const
    {
        std::vector<TravelNode> nodes = _nodes;
        const uint32_t startIndex = static_cast<uint32_t>(nodes.size());
        nodes.push_back({ StartNodeId, start, TravelNodeKind::Waypoint, 0 });
        const uint32_t destinationIndex = static_cast<uint32_t>(nodes.size());
        nodes.push_back({ DestinationNodeId, destination, TravelNodeKind::Waypoint, 0 });

        uint32_t homeIndex = std::numeric_limits<uint32_t>::max();
        if (options.canUseHearthstone)
        {
            homeIndex = static_cast<uint32_t>(nodes.size());
            nodes.push_back({ HomeNodeId, options.home, TravelNodeKind::Home, 0 });
        }

        std::vector<TravelEdge> edges = _edges;
        auto addDynamicEdge = [&](uint32_t fromIndex, uint32_t toIndex, TravelMode mode,
                                  float cost, uint32_t reference = 0) {
            const TravelNode& from = nodes[fromIndex];
            const TravelNode& to = nodes[toIndex];
            uint64_t key = MakeEdgeKey(from.id, to.id, mode, reference);
            edges.push_back({ fromIndex, toIndex, mode, std::max(1.0f, cost), reference, key });
        };

        auto connectToNearest = [&](uint32_t dynamicIndex, bool outgoing) {
            std::vector<std::pair<float, uint32_t>> nearest;
            for (uint32_t i = 0; i < _nodes.size(); ++i)
            {
                if (options.blockedNodes.contains(nodes[i].id))
                    continue;
                if (nodes[i].kind == TravelNodeKind::FlightMaster &&
                    !options.usableTaxiNodes.contains(nodes[i].reference))
                    continue;
                if (nodes[i].position.mapId != nodes[dynamicIndex].position.mapId)
                    continue;
                if (outgoing && !CanArriveByWalking(nodes[i].kind))
                    continue;
                if (!outgoing && !CanDepartByWalking(nodes[i].kind))
                    continue;
                nearest.emplace_back(Distance(nodes[i].position, nodes[dynamicIndex].position), i);
            }
            std::sort(nearest.begin(), nearest.end());
            if (nearest.size() > ConnectorCount)
                nearest.resize(ConnectorCount);
            for (const auto& [distance, nodeIndex] : nearest)
            {
                if (outgoing)
                    addDynamicEdge(dynamicIndex, nodeIndex, TravelMode::Walk, distance);
                else
                    addDynamicEdge(nodeIndex, dynamicIndex, TravelMode::Walk, distance);
            }
            return !nearest.empty();
        };

        if (!options.localGroundOnly)
        {
            connectToNearest(startIndex, true);
            connectToNearest(destinationIndex, false);
        }
        if (homeIndex != std::numeric_limits<uint32_t>::max() &&
            !options.localGroundOnly)
        {
            float startToDest = (start.mapId == destination.mapId)
                ? Distance(start, destination) : std::numeric_limits<float>::max();
            float homeToDest = (options.home.mapId == destination.mapId)
                ? Distance(options.home, destination) : std::numeric_limits<float>::max();

            // Hearthstone should only be considered if it brings the bot significantly
            // closer to the destination than current position, or transfers to destination map.
            if (options.home.mapId == destination.mapId &&
                (start.mapId != destination.mapId || homeToDest < startToDest - 150.0f))
            {
                addDynamicEdge(startIndex, homeIndex, TravelMode::Hearthstone, 120.0f);
            }

            connectToNearest(homeIndex, true);
            if (options.home.mapId == destination.mapId &&
                Distance(options.home, destination) <= DirectWalkDistance)
            {
                addDynamicEdge(homeIndex, destinationIndex, TravelMode::Walk, Distance(options.home, destination));
            }
        }

        if (start.mapId == destination.mapId)
        {
            constexpr float MaxDirectWalkDistance = 6000.0f;
            float directDist = Distance(start, destination);
            if (directDist <= MaxDirectWalkDistance)
            {
                // Always attempt the real ground route before using graph anchors
                // on the same map if within reasonable direct walking range.
                addDynamicEdge(startIndex, destinationIndex, TravelMode::Walk,
                    directDist <= DirectWalkDistance ? 1.0f : directDist);
            }
        }

        std::vector<std::vector<uint32_t>> adjacency(nodes.size());
        for (uint32_t i = 0; i < edges.size(); ++i)
        {
            if (edges[i].from < adjacency.size() && edges[i].to < nodes.size())
                adjacency[edges[i].from].push_back(i);
        }

        const float infinity = std::numeric_limits<float>::max();
        std::vector<float> distance(nodes.size(), infinity);
        std::vector<int32_t> previousEdge(nodes.size(), -1);
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
        distance[startIndex] = 0.0f;
        queue.push({ 0.0f, startIndex });

        while (!queue.empty())
        {
            QueueEntry current = queue.top();
            queue.pop();
            if (current.cost != distance[current.index])
                continue;
            if (current.index == destinationIndex)
                break;

            for (uint32_t edgeIndex : adjacency[current.index])
            {
                const TravelEdge& edge = edges[edgeIndex];
                if (options.blockedEdges.contains(edge.key))
                    continue;
                const TravelNode& fromNode = nodes[edge.from];
                const TravelNode& toNode = nodes[edge.to];
                if (options.blockedNodes.contains(fromNode.id) ||
                    options.blockedNodes.contains(toNode.id))
                    continue;
                if ((fromNode.kind == TravelNodeKind::FlightMaster &&
                     !options.usableTaxiNodes.contains(fromNode.reference)) ||
                    (toNode.kind == TravelNodeKind::FlightMaster &&
                     !options.usableTaxiNodes.contains(toNode.reference)))
                    continue;
                if (edge.mode == TravelMode::FlightPath)
                {
                    uint32_t sourceTaxi = fromNode.reference;
                    uint32_t destinationTaxi = toNode.reference;
                    if (!options.usableTaxiNodes.contains(sourceTaxi) ||
                        !options.usableTaxiNodes.contains(destinationTaxi))
                    {
                        continue;
                    }
                }

                float candidate = current.cost + edge.cost;
                if (candidate >= distance[edge.to])
                    continue;
                distance[edge.to] = candidate;
                previousEdge[edge.to] = static_cast<int32_t>(edgeIndex);
                queue.push({ candidate, edge.to });
            }
        }

        if (previousEdge[destinationIndex] < 0)
            return {};

        std::vector<uint32_t> reversed;
        for (uint32_t nodeIndex = destinationIndex; nodeIndex != startIndex;)
        {
            int32_t edgeIndex = previousEdge[nodeIndex];
            if (edgeIndex < 0)
                return {};
            reversed.push_back(static_cast<uint32_t>(edgeIndex));
            nodeIndex = edges[edgeIndex].from;
        }
        std::reverse(reversed.begin(), reversed.end());

        std::vector<RouteStep> route;
        route.reserve(reversed.size());
        for (uint32_t edgeIndex : reversed)
        {
            const TravelEdge& edge = edges[edgeIndex];
            const TravelNode& from = nodes[edge.from];
            const TravelNode& to = nodes[edge.to];
            route.push_back({ edge.mode, from.position, to.position, from.id, to.id,
                from.reference, to.reference, edge.reference, edge.key });
        }
        return route;
    }
}
