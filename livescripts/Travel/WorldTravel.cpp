#include "WorldTravel.h"

#include "Cache/BotCache.h"
#include "CellImpl.h"
#include "Config/BotConfig.h"
#include "Creature.h"
#include "DataStores/DBCStores.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Entities/Transport/Transport.h"
#include "GameObject.h"
#include "Globals/ObjectMgr.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Helper/MovementManager.h"
#include "Helper/TeleportUtils.h"
#include "Helper/TimeUtils.h"
#include "Travel/WorldTravelPolicy.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "TransportMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <unordered_map>

namespace Travel
{
    namespace
    {
        constexpr uint32_t HearthstoneItemId = 6948;
        constexpr float StepArrivalRange = 18.0f;
        constexpr float TransportArrivalRange = 70.0f;
        constexpr float TransportDockRange = 30.0f;
        constexpr float TransitArrivalRange = 45.0f;
        constexpr uint32_t WalkNoProgressTimeoutMs = 20000;
        constexpr uint32_t WalkHardTimeoutMs = 10 * 60 * 1000;
        constexpr uint32_t FlightTimeoutMs = 6 * 60 * 1000;
        constexpr uint32_t TransportTimeoutMs = 7 * 60 * 1000;
        constexpr uint32_t PortalTimeoutMs = 20000;
        constexpr uint32_t TravelHardTimeoutMs = 12 * 60 * 1000;
        constexpr uint32_t MaxReplans = 8;
        constexpr float TravelLegDistance = 35.0f;
        constexpr float TravelThreatClearance = 18.0f;
        constexpr float TravelDetourDistance = 28.0f;
        constexpr float LocalGroundOnlyDistance = 1500.0f;
        constexpr std::size_t MaximumWalkingProbesPerUpdate = 3;
        constexpr uint32_t WalkingProbeCooldownMs = 1000;

        struct LocalTravelThreat
        {
            Brain::DangerArea area;
        };

        Common::PositionInfo BoundedLegEndpoint(Player* bot,
            const Common::PositionInfo& destination, float maximumDistance)
        {
            Common::PositionInfo endpoint = destination;
            if (!bot || bot->GetMapId() != destination.mapId)
                return endpoint;

            float dx = destination.x - bot->GetPositionX();
            float dy = destination.y - bot->GetPositionY();
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= maximumDistance || distance <= 0.001f)
                return endpoint;

            float scale = maximumDistance / distance;
            endpoint.x = bot->GetPositionX() + dx * scale;
            endpoint.y = bot->GetPositionY() + dy * scale;
            // Probe the bot's current vertical layer, snapping to the actual ground
            // surface when available so incline slopes do not fail navmesh queries.
            endpoint.z = bot->GetPositionZ();
            if (Map const* map = bot->GetMap())
            {
                float groundZ = map->GetHeight(bot->GetPhaseMask(), endpoint.x, endpoint.y, endpoint.z, true, 50.0f);
                if (std::isfinite(groundZ) && std::fabs(groundZ - endpoint.z) < 25.0f)
                    endpoint.z = groundZ;
            }
            return endpoint;
        }

        bool IsUnsafeTravelThreat(Player* bot, Creature* hostile)
        {
            if (!bot || !hostile || !hostile->IsAlive() ||
                hostile->GetMap() != bot->GetMap())
            {
                return false;
            }

            bool engaged = hostile->GetVictim() == bot || hostile->IsInCombatWith(bot);
            if (!engaged && !hostile->CanStartAttack(bot, false))
                return false;

            int32_t levelDelta = static_cast<int32_t>(hostile->GetLevel()) -
                static_cast<int32_t>(bot->GetLevel());
            return levelDelta > Config::BotConfig::GetQuestMaxLevelsAboveBot();
        }

        std::vector<LocalTravelThreat> FindNearbyUnsafeTravelThreats(Player* bot,
            const std::vector<ObjectGuid>& nearbyHostileGuids,
            uint32_t nowSec)
        {
            std::vector<LocalTravelThreat> threats;
            for (ObjectGuid guid : nearbyHostileGuids)
            {
                Creature* hostile = ObjectAccessor::GetCreature(*bot, guid);
                if (!IsUnsafeTravelThreat(bot, hostile))
                    continue;

                Brain::DangerArea area{ hostile->GetMapId(), hostile->GetPositionX(),
                    hostile->GetPositionY(), TravelThreatClearance, nowSec + 1 };
                threats.push_back({ area });
            }
            return threats;
        }

        bool CandidateCrossesDanger(Player* bot,
            const Common::PositionInfo& endpoint,
            const std::vector<Brain::DangerArea>& dangerAreas,
            const std::vector<LocalTravelThreat>& localThreats,
            uint32_t nowSec)
        {
            for (const Brain::DangerArea& area : dangerAreas)
            {
                if (area.BlocksTravelSegment(bot->GetMapId(), bot->GetPositionX(),
                    bot->GetPositionY(), endpoint.x, endpoint.y, nowSec))
                {
                    return true;
                }
            }
            for (const LocalTravelThreat& threat : localThreats)
            {
                if (threat.area.IntersectsSegment(bot->GetMapId(),
                    bot->GetPositionX(), bot->GetPositionY(), endpoint.x,
                    endpoint.y, nowSec))
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<Common::PositionInfo> GenerateDetourEndpoints(Player const* bot,
            const Common::PositionInfo& destination)
        {
            std::vector<Common::PositionInfo> detours;
            if (!bot)
                return detours;

            float heading = std::atan2(destination.y - bot->GetPositionY(),
                destination.x - bot->GetPositionX());
            constexpr float Pi = 3.14159265358979323846f;
            // When the full destination corridor is unavailable, explore the
            // complete local topology. A ramp or switchback may begin sideways
            // or behind the final bearing.
            constexpr float DetourAngles[] = {
                Pi / 6.0f, -Pi / 6.0f,
                Pi / 3.0f, -Pi / 3.0f,
                Pi / 2.0f, -Pi / 2.0f,
                2.0f * Pi / 3.0f, -2.0f * Pi / 3.0f,
                5.0f * Pi / 6.0f, -5.0f * Pi / 6.0f,
                Pi
            };
            Map const* map = bot->GetMap();
            for (float angleOffset : DetourAngles)
            {
                float targetX = bot->GetPositionX() + std::cos(heading + angleOffset) * TravelDetourDistance;
                float targetY = bot->GetPositionY() + std::sin(heading + angleOffset) * TravelDetourDistance;
                float targetZ = bot->GetPositionZ();
                if (map)
                {
                    float groundZ = map->GetHeight(bot->GetPhaseMask(), targetX, targetY, targetZ, true, 50.0f);
                    if (std::isfinite(groundZ) && std::fabs(groundZ - targetZ) < 25.0f)
                        targetZ = groundZ;
                }
                detours.push_back({ targetX, targetY, targetZ, bot->GetMapId() });
            }
            return detours;
        }

        bool StartSafeWalkingLeg(Player* bot, MovementManager* movement,
            const RouteStep& step,
            const std::vector<Brain::DangerArea>& dangerAreas,
            const std::vector<ObjectGuid>& nearbyHostileGuids,
            std::vector<Helper::MovementPathPolicy::Point>& visitedFrontiers,
            std::size_t& candidateCursor)
        {
            uint32_t nowSec = Helper::MonotonicSeconds();
            Common::PositionInfo directEndpoint = BoundedLegEndpoint(
                bot, step.to, TravelLegDistance);
            std::vector<LocalTravelThreat> localThreats =
                FindNearbyUnsafeTravelThreats(bot, nearbyHostileGuids, nowSec);

            struct Candidate
            {
                Common::PositionInfo destination;
                Common::PositionInfo endpoint;
            };
            std::vector<Candidate> candidates;
            candidates.push_back({ step.to, directEndpoint });
            for (const Common::PositionInfo& detour : GenerateDetourEndpoints(bot, step.to))
                candidates.push_back({ detour, detour });

            std::size_t inspectedCandidates = 0;
            std::size_t pathAttempts = 0;
            while (inspectedCandidates < candidates.size() &&
                pathAttempts < MaximumWalkingProbesPerUpdate)
            {
                std::size_t candidateIndex = candidateCursor % candidates.size();
                candidateCursor = (candidateIndex + 1) % candidates.size();
                ++inspectedCandidates;
                const Candidate& candidate = candidates[candidateIndex];
                Helper::MovementPathPolicy::Point current{
                    bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()
                };
                Helper::MovementPathPolicy::Point candidateEndpoint{
                    candidate.endpoint.x, candidate.endpoint.y, candidate.endpoint.z
                };
                if (Helper::MovementPathPolicy::Distance2D(current,
                    candidateEndpoint) < 4.0f)
                    continue;
                if (CandidateCrossesDanger(bot, candidate.endpoint, dangerAreas,
                    localThreats, nowSec))
                {
                    continue;
                }
                uint64_t pathGeneration = movement->GetPathAttemptGeneration();
                if (movement->MoveToTravel(candidate.destination.x,
                    candidate.destination.y, candidate.destination.z,
                    BotMovementState::Moving, false))
                {
                    if (movement->GetPathAttemptGeneration() != pathGeneration)
                        ++pathAttempts;
                    float endpointX = 0.0f, endpointY = 0.0f, endpointZ = 0.0f;
                    if (!movement->GetActivePathEndpoint(endpointX, endpointY, endpointZ))
                    {
                        movement->Stop();
                        continue;
                    }
                    Common::PositionInfo actualEndpoint{
                        endpointX, endpointY, endpointZ, bot->GetMapId()
                    };
                    Helper::MovementPathPolicy::Point actualPoint{
                        endpointX, endpointY, endpointZ
                    };
                    if (Helper::MovementPathPolicy::Distance2D(current,
                            actualPoint) < 4.0f ||
                        !Helper::MovementPathPolicy::IsDistinctLayeredEndpoint(
                            visitedFrontiers, actualPoint, 6.0f) ||
                        CandidateCrossesDanger(bot, actualEndpoint, dangerAreas,
                            localThreats, nowSec))
                    {
                        movement->Stop();
                        continue;
                    }

                    visitedFrontiers.push_back(actualPoint);
                    constexpr std::size_t MaximumVisitedFrontiers = 32;
                    if (visitedFrontiers.size() > MaximumVisitedFrontiers)
                        visitedFrontiers.erase(visitedFrontiers.begin());
                    candidateCursor = 0;
                    return true;
                }
                if (movement->GetPathAttemptGeneration() != pathGeneration)
                    ++pathAttempts;
            }
            return false;
        }

        bool HasSafeWalkingPreflight(Player* bot, const RouteStep& step,
            const std::vector<Brain::DangerArea>& dangerAreas,
            const std::vector<ObjectGuid>& nearbyHostileGuids)
        {
            if (!bot || bot->GetMapId() != step.to.mapId)
                return false;

            uint32_t nowSec = Helper::MonotonicSeconds();
            std::vector<LocalTravelThreat> localThreats =
                FindNearbyUnsafeTravelThreats(bot, nearbyHostileGuids, nowSec);
            std::vector<Common::PositionInfo> endpoints;
            endpoints.push_back(BoundedLegEndpoint(bot, step.to,
                TravelLegDistance));
            std::vector<Common::PositionInfo> detours = GenerateDetourEndpoints(bot, step.to);
            endpoints.insert(endpoints.end(), detours.begin(), detours.end());

            for (const Common::PositionInfo& endpoint : endpoints)
            {
                float dx = endpoint.x - bot->GetPositionX();
                float dy = endpoint.y - bot->GetPositionY();
                if (dx * dx + dy * dy < 16.0f ||
                    CandidateCrossesDanger(bot, endpoint, dangerAreas,
                        localThreats, nowSec))
                {
                    continue;
                }
                if (Helper::TeleportUtils::HasCompleteGroundPathTo(bot,
                    endpoint.x, endpoint.y, endpoint.z))
                {
                    return true;
                }
            }
            return false;
        }

        TravelGraph BuildWorldGraph()
        {
            TravelGraph graph;
            std::unordered_map<uint32_t, uint32_t> taxiNodes;

            for (TaxiNodesEntry const* taxi : sTaxiNodesStore)
            {
                if (!taxi || taxi->ID == 0)
                    continue;
                Common::PositionInfo position{ taxi->Pos.X, taxi->Pos.Y, taxi->Pos.Z, taxi->ContinentID };
                taxiNodes[taxi->ID] = graph.AddNode(position, TravelNodeKind::FlightMaster, taxi->ID);
            }

            for (const auto& [sourceTaxi, destinations] : sTaxiPathSetBySource)
            {
                auto source = taxiNodes.find(sourceTaxi);
                if (source == taxiNodes.end())
                    continue;
                for (const auto& [destinationTaxi, path] : destinations)
                {
                    auto destination = taxiNodes.find(destinationTaxi);
                    if (destination == taxiNodes.end() || path.ID == 0)
                        continue;
                    float distance = TravelGraph::Distance(
                        graph.GetNode(source->second)->position,
                        graph.GetNode(destination->second)->position);
                    if (!std::isfinite(distance))
                        distance = 2000.0f;
                    graph.AddEdge(source->second, destination->second, TravelMode::FlightPath,
                        45.0f + distance * 0.16f, path.ID);
                }
            }

            for (const auto& [entry, gameObjectTemplate] : sObjectMgr->GetGameObjectTemplates())
            {
                if (gameObjectTemplate.type == GAMEOBJECT_TYPE_MO_TRANSPORT)
                {
                    TransportTemplate const* transport = sTransportMgr->GetTransportTemplate(entry);
                    if (!transport)
                        continue;

                    std::vector<uint32_t> stops;
                    for (const KeyFrame& frame : transport->keyFrames)
                    {
                        if (!frame.Node || !frame.IsStopFrame())
                            continue;
                        Common::PositionInfo position{ frame.Node->Loc.X, frame.Node->Loc.Y,
                            frame.Node->Loc.Z, frame.Node->ContinentID };
                        if (!stops.empty())
                        {
                            const TravelNode* previous = graph.GetNode(stops.back());
                            if (previous && previous->position.mapId == position.mapId &&
                                TravelGraph::Distance(previous->position, position) < 5.0f)
                            {
                                continue;
                            }
                        }
                        stops.push_back(graph.AddNode(position, TravelNodeKind::TransportStop, entry));
                    }

                    if (stops.size() >= 2)
                    {
                        for (std::size_t i = 0; i < stops.size(); ++i)
                        {
                            std::size_t next = (i + 1) % stops.size();
                            graph.AddEdge(stops[i], stops[next], TravelMode::Transport, 420.0f, entry);
                        }
                    }
                    continue;
                }

                uint32_t portalSpellId = 0;
                if (gameObjectTemplate.type == GAMEOBJECT_TYPE_SPELLCASTER)
                    portalSpellId = gameObjectTemplate.spellcaster.spellId;
                else if (gameObjectTemplate.type == GAMEOBJECT_TYPE_GOOBER)
                    portalSpellId = gameObjectTemplate.goober.spellId;
                if (portalSpellId == 0)
                    continue;

                SpellInfo const* portalSpell = sSpellMgr->GetSpellInfo(portalSpellId);
                if (!portalSpell || !portalSpell->HasEffect(SPELL_EFFECT_TELEPORT_UNITS))
                    continue;

                SpellTargetPosition const* target = nullptr;
                for (SpellEffectInfo const& effect : portalSpell->GetEffects())
                {
                    target = sSpellMgr->GetSpellTargetPosition(portalSpellId, effect.EffectIndex);
                    if (target)
                        break;
                }
                if (!target)
                    continue;

                Common::PositionInfo destination{ target->target_X, target->target_Y,
                    target->target_Z, target->target_mapId };
                uint32_t destinationNode = std::numeric_limits<uint32_t>::max();
                for (const Common::PositionInfo& sourcePosition : Cache::BotCache::GetGameObjectLocations(entry))
                {
                    if (!TravelGraph::IsGlobalPortalTransition(sourcePosition, destination))
                        continue;
                    if (destinationNode == std::numeric_limits<uint32_t>::max())
                    {
                        destinationNode = graph.AddNode(destination,
                            TravelNodeKind::PortalDestination, portalSpellId);
                    }
                    uint32_t sourceNode = graph.AddNode(sourcePosition, TravelNodeKind::Portal, entry);
                    graph.AddEdge(sourceNode, destinationNode, TravelMode::Portal, 25.0f, entry);
                }
            }

            graph.AddLocalWalkConnections();
            return graph;
        }

        TravelGraph& GetWorldGraph()
        {
            static TravelGraph graph = BuildWorldGraph();
            return graph;
        }

        Creature* FindNearbyFlightMaster(Player* bot, float range)
        {
            if (!bot || !bot->IsInWorld())
                return nullptr;

            std::list<Creature*> creatures;
            Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, range);
            Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
            Cell::VisitGridObjects(bot, searcher, range);

            Creature* nearest = nullptr;
            float nearestDistance = std::numeric_limits<float>::max();
            for (Creature* creature : creatures)
            {
                if (!creature || !creature->IsAlive() ||
                    !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_FLIGHTMASTER) ||
                    creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
                {
                    continue;
                }
                float distance = bot->GetDistance(creature);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = creature;
                }
            }
            return nearest;
        }

        void DiscoverNearbyFlightPath(Player* bot)
        {
            if (Creature* flightMaster = FindNearbyFlightMaster(bot, 35.0f))
                bot->GetSession()->SendLearnNewTaxiNode(flightMaster);
        }

        Creature* FindNearbyInnkeeper(Player* bot, float range)
        {
            if (!bot || !bot->IsInWorld())
                return nullptr;

            std::list<Creature*> creatures;
            Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, range);
            Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
            Cell::VisitGridObjects(bot, searcher, range);

            Creature* nearest = nullptr;
            float nearestDistance = std::numeric_limits<float>::max();
            for (Creature* creature : creatures)
            {
                if (!creature || !creature->IsAlive() ||
                    !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_INNKEEPER) ||
                    creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
                {
                    continue;
                }
                float distance = bot->GetDistance(creature);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = creature;
                }
            }
            return nearest;
        }

        void DiscoverNearbyInnkeeper(Player* bot)
        {
            if (Creature* innkeeper = FindNearbyInnkeeper(bot, 35.0f))
            {
                if (Helper::TeleportUtils::ShouldUpdateHomebind(bot, innkeeper))
                {
                    Helper::TeleportUtils::SetHomebind(bot, innkeeper->GetMapId(),
                        innkeeper->GetAreaId(), innkeeper->GetPositionX(),
                        innkeeper->GetPositionY(), innkeeper->GetPositionZ());
                }
            }
        }

        bool CanUseHearthstone(Player* bot)
        {
            if (!bot || bot->IsInCombat() || bot->m_homebindMapId == UINT32_MAX)
                return false;
            Item* hearthstone = bot->GetItemByEntry(HearthstoneItemId);
            return hearthstone && bot->CanUseItem(hearthstone) == EQUIP_ERR_OK;
        }

        RouteOptions BuildRouteOptions(Player* bot,
            const std::unordered_set<uint64_t>& blockedEdges,
            const std::unordered_set<uint32_t>& blockedNodes,
            bool hearthstoneBlocked = false)
        {
            RouteOptions options;
            if (!bot)
                return options;

            for (const TravelNode& node : GetWorldGraph().GetNodes())
            {
                if (node.kind != TravelNodeKind::FlightMaster ||
                    !bot->m_taxi.IsTaximaskNodeKnown(node.reference))
                    continue;

                TaxiNodesEntry const* taxi = sTaxiNodesStore.LookupEntry(node.reference);
                uint8_t teamIndex = static_cast<uint8_t>(bot->GetTeamId());
                if (taxi && teamIndex < 2 && taxi->MountCreatureID[teamIndex] != 0)
                {
                    options.usableTaxiNodes.insert(node.reference);
                }
            }
            options.blockedEdges = blockedEdges;
            options.blockedNodes = blockedNodes;
            options.canUseHearthstone =
                WorldTravelPolicy::CanPlanHearthstone(
                    CanUseHearthstone(bot), hearthstoneBlocked);
            options.home = { bot->m_homebindX, bot->m_homebindY,
                bot->m_homebindZ, bot->m_homebindMapId };
            return options;
        }

        uint32_t FindUsableMountSpell(Player* bot)
        {
            if (!bot || bot->GetLevel() < 20 || bot->IsMounted() || bot->IsInCombat() ||
                bot->IsInWater() || bot->IsNonMeleeSpellCast(false))
            {
                return 0;
            }

            uint32_t bestSpell = 0;
            uint32_t bestLevel = 0;
            for (const auto& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (playerSpell.state == PLAYERSPELL_REMOVED || playerSpell.disabled || !playerSpell.active)
                    continue;
                SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
                if (!spell || !spell->HasAura(SPELL_AURA_MOUNTED) ||
                    spell->SpellLevel > bot->GetLevel() || bot->GetSpellHistory()->HasCooldown(spell))
                {
                    continue;
                }
                if (spell->SpellLevel >= bestLevel)
                {
                    bestLevel = spell->SpellLevel;
                    bestSpell = spellId;
                }
            }
            return bestSpell;
        }

        bool SameDestination(const Common::PositionInfo& left, const Common::PositionInfo& right)
        {
            return left.mapId == right.mapId && TravelGraph::Distance(left, right) < 5.0f;
        }

        bool IsNear(Player* bot, const Common::PositionInfo& position, float range)
        {
            if (!bot || bot->GetMapId() != position.mapId)
                return false;
            Common::PositionInfo current{ bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId() };
            return TravelGraph::Distance(current, position) <= range;
        }
    }

    bool WorldTravel::NeedsTravel(Player* bot, const Common::PositionInfo& destination, float sameMapDistance)
    {
        if (!bot || bot->GetMapId() != destination.mapId)
            return true;
        Common::PositionInfo current{ bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId() };
        return TravelGraph::Distance(current, destination) > sameMapDistance;
    }

    bool WorldTravel::CanReach(Player* bot, const Common::PositionInfo& destination)
    {
        if (!bot || !bot->IsInWorld())
            return false;
        if (bot->GetMapId() == destination.mapId)
            return true;

        Common::PositionInfo start{ bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), bot->GetMapId() };
        static const std::unordered_set<uint64_t> noBlockedEdges;
        static const std::unordered_set<uint32_t> noBlockedNodes;
        RouteOptions options = BuildRouteOptions(bot, noBlockedEdges, noBlockedNodes);
        return !GetWorldGraph().FindRoute(start, destination, options).empty();
    }

    TravelPreflightResult WorldTravel::Preflight(Player* bot,
        const Common::PositionInfo& destination,
        const std::vector<Brain::DangerArea>& dangerAreas,
        const std::vector<ObjectGuid>& nearbyHostileGuids)
    {
        if (!bot || !bot->IsInWorld())
            return { TravelPreflightStatus::Unreachable,
                "bot was unavailable for quest travel preflight" };
        if (IsNear(bot, destination, StepArrivalRange))
            return { TravelPreflightStatus::Ready, {} };
        if (bot->IsBeingTeleported() || bot->IsNonMeleeSpellCast(false))
            return { TravelPreflightStatus::Deferred,
                "quest travel preflight deferred during an active transition or cast" };

        Common::PositionInfo start{ bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), bot->GetMapId() };
        static const std::unordered_set<uint64_t> noBlockedEdges;
        static const std::unordered_set<uint32_t> noBlockedNodes;
        RouteOptions options = BuildRouteOptions(bot, noBlockedEdges,
            noBlockedNodes);
        options.localGroundOnly = start.mapId == destination.mapId &&
            TravelGraph::Distance(start, destination) <= LocalGroundOnlyDistance;
        std::vector<RouteStep> route = GetWorldGraph().FindRoute(start,
            destination, options);
        auto failPreflight = [&](const char* reason) -> TravelPreflightResult {
            if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
            {
                Diagnostics::StructuredEvent event;
                event.event = "travel_preflight_rejected";
                event.goal = "Travel";
                event.action = "WorldTravel";
                event.requestX = destination.x;
                event.requestY = destination.y;
                event.requestZ = destination.z;
                event.outcome = "Unreachable";
                event.details = reason;
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));
            }
            Diagnostics::BotTrace::LogToFile(bot, "Travel",
                std::string("Preflight rejected: ") + reason, Diagnostics::LogEvent::Normal);
            return { TravelPreflightStatus::Unreachable, reason };
        };

        if (route.empty())
        {
            return failPreflight("no travel-graph route connects the current position to the quest destination");
        }

        float totalWalkingDistance = 0.0f;
        for (const RouteStep& step : route)
        {
            if (step.mode == TravelMode::Walk)
                totalWalkingDistance += TravelGraph::Distance(step.from, step.to);
        }

        constexpr float MaximumWalkRouteThreshold = 6000.0f;
        if (totalWalkingDistance > MaximumWalkRouteThreshold)
        {
            return failPreflight("quest travel route exceeds maximum walk threshold without flight transport");
        }

        const RouteStep& firstStep = route.front();
        if (firstStep.mode != TravelMode::Walk ||
            HasSafeWalkingPreflight(bot, firstStep, dangerAreas,
                nearbyHostileGuids))
        {
            return { TravelPreflightStatus::Ready, {} };
        }
        return failPreflight("no safe executable first ground leg reaches the selected quest route");
    }

    void WorldTravel::Reset()
    {
        _route.clear();
        _stepIndex = 0;
        _blockedEdges.clear();
        _blockedNodes.clear();
        _stepElapsedMs = 0;
        _elapsedMs = 0;
        _replanCount = 0;
        _walkPrePathWaitMs = 0;
        _walkProbeCooldownMs = 0;
        _walkCandidateCursor = 0;
        _stepPathAttemptGeneration = 0;
        _walkProgress.Reset();
        _visitedWalkFrontiers.clear();
        _waitReason = TravelWaitReason::None;
        _stepPathBaselineSet = false;
        _transitionStarted = false;
        _mountAttempted = false;
        _hearthstoneBlocked = false;
        _hasDestination = false;
        _active = false;
        _failed = false;
        _hasFailureArea = false;
        _failureArea = {};
        _failureReason.clear();
        _hearthstoneFailureCount = 0;
    }

    bool WorldTravel::GetFailureArea(Brain::DangerArea& area) const
    {
        if (!_hasFailureArea)
            return false;
        area = _failureArea;
        return true;
    }

    void WorldTravel::Stop(Player* bot, MovementManager* movement)
    {
        if (bot && bot->GetTransport())
            bot->GetTransport()->RemovePassenger(bot);
        if (movement)
            movement->Stop();
        Reset();
    }

    const char* WorldTravel::GetCurrentModeName() const
    {
        if (_stepIndex >= _route.size())
            return "None";
        switch (_route[_stepIndex].mode)
        {
            case TravelMode::Walk: return "Walk";
            case TravelMode::FlightPath: return "FlightPath";
            case TravelMode::Transport: return "Transport";
            case TravelMode::Portal: return "Portal";
            case TravelMode::Hearthstone: return "Hearthstone";
            default: return "Unknown";
        }
    }

    const char* WorldTravel::GetWaitReasonName() const
    {
        switch (_waitReason)
        {
            case TravelWaitReason::MountCast: return "MountCast";
            case TravelWaitReason::Casting: return "Casting";
            case TravelWaitReason::PathRequest: return "PathRequest";
            case TravelWaitReason::Walking: return "Walking";
            case TravelWaitReason::Flight: return "Flight";
            case TravelWaitReason::FlightMaster: return "FlightMaster";
            case TravelWaitReason::Transport: return "Transport";
            case TravelWaitReason::Portal: return "Portal";
            case TravelWaitReason::Hearthstone: return "Hearthstone";
            default: return "None";
        }
    }

    bool WorldTravel::BuildRoute(Player* bot)
    {
        RouteOptions options = BuildRouteOptions(bot, _blockedEdges,
            _blockedNodes, _hearthstoneBlocked);

        Common::PositionInfo start{ bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId() };
        options.localGroundOnly = start.mapId == _destination.mapId &&
            TravelGraph::Distance(start, _destination) <= LocalGroundOnlyDistance;
        _route = GetWorldGraph().FindRoute(start, _destination, options);
        _stepIndex = 0;
        _stepElapsedMs = 0;
        _walkPrePathWaitMs = 0;
        _walkProbeCooldownMs = 0;
        _walkCandidateCursor = 0;
        _walkProgress.Reset();
        _waitReason = TravelWaitReason::None;
        _stepPathBaselineSet = false;
        _transitionStarted = false;
        _mountAttempted = false;

        if (_route.empty())
        {
            _failureReason = "no travel-graph route connects the current position to the destination map";
            _failed = true;
            return false;
        }

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Travel] Bot '{}' planned {} travel steps from map {} to map {}",
                bot->GetName(), _route.size(), start.mapId, _destination.mapId);
        }
        return true;
    }

    TravelResult WorldTravel::Update(Player* bot, MovementManager* movement,
        Common::PositionInfo destination, uint32_t deltaMs,
        const std::vector<Brain::DangerArea>& dangerAreas,
        const std::vector<ObjectGuid>& nearbyHostileGuids)
    {
        if (!bot || !bot->IsInWorld() || !movement)
        {
            _failureReason = "bot or movement manager was unavailable during world travel";
            return TravelResult::Failed;
        }

        if (!_active || !SameDestination(_destination, destination))
        {
            Reset();
            _destination = destination;
            _hasDestination = true;
            _active = true;
        }

        _elapsedMs = deltaMs > std::numeric_limits<uint32_t>::max() - _elapsedMs
            ? std::numeric_limits<uint32_t>::max() : _elapsedMs + deltaMs;
        if (_elapsedMs >= TravelHardTimeoutMs)
        {
            if (movement)
                movement->Stop();
            _failureReason = "world travel exceeded its 12-minute hard timeout";
            _failed = true;
            return TravelResult::Failed;
        }

        if (IsNear(bot, _destination, StepArrivalRange))
        {
            _active = false;
            return TravelResult::Arrived;
        }
        if (_failed)
            return TravelResult::Failed;
        if (_route.empty() && !BuildRoute(bot))
            return TravelResult::Failed;

        return UpdateStep(bot, movement, deltaMs, dangerAreas,
            nearbyHostileGuids);
    }

    void WorldTravel::RecordProgress(Player* bot,
        const Common::PositionInfo& waypoint, uint32_t deltaMs)
    {
        _stepElapsedMs += deltaMs;
        if (bot->GetMapId() != waypoint.mapId)
            return;

        _walkProgress.Observe(
            { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() },
            { waypoint.x, waypoint.y, waypoint.z }, deltaMs);
    }

    void WorldTravel::CompleteStep(Player* bot)
    {
        DiscoverNearbyFlightPath(bot);
        DiscoverNearbyInnkeeper(bot);
        if (_stepIndex < _route.size())
        {
            TravelMode mode = _route[_stepIndex].mode;
            if (mode == TravelMode::FlightPath || mode == TravelMode::Transport || mode == TravelMode::Hearthstone)
            {
                if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
                {
                    Diagnostics::StructuredEvent event;
                    event.event = "travel_step_completed";
                    event.goal = "Travel";
                    event.action = "WorldTravel";
                    event.requestX = _destination.x;
                    event.requestY = _destination.y;
                    event.requestZ = _destination.z;
                    event.outcome = "Succeeded";
                    event.details = std::string("mode=") + GetCurrentModeName();
                    Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                }
                Diagnostics::BotTrace::LogToFile(bot, "Travel",
                    std::string("Completed travel step: ") + GetCurrentModeName(),
                    Diagnostics::LogEvent::Normal);
            }
        }
        ++_stepIndex;
        _stepElapsedMs = 0;
        _walkPrePathWaitMs = 0;
        _walkProbeCooldownMs = 0;
        _walkCandidateCursor = 0;
        _walkProgress.Reset();
        _visitedWalkFrontiers.clear();
        _waitReason = TravelWaitReason::None;
        _stepPathBaselineSet = false;
        _transitionStarted = false;
        _mountAttempted = false;
        if (_stepIndex >= _route.size())
            _route.clear();
    }

    void WorldTravel::FailStep(Player* bot, MovementManager* movement, std::string reason)
    {
        if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
        {
            Diagnostics::StructuredEvent event;
            event.event = "travel_step_failed";
            event.goal = "Travel";
            event.action = "WorldTravel";
            event.requestX = _destination.x;
            event.requestY = _destination.y;
            event.requestZ = _destination.z;
            event.outcome = "Failed";
            event.details = std::string("mode=") + GetCurrentModeName() +
                ";replan=" + std::to_string(_replanCount) + ";reason=" + reason;
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
        }
        Diagnostics::BotTrace::LogToFile(bot, "Travel",
            std::string("Travel step failed (mode=") + GetCurrentModeName() +
            ", replan=" + std::to_string(_replanCount) + "): " + reason,
            Diagnostics::LogEvent::Normal);

        if (_stepIndex < _route.size())
        {
            RouteStep const& failedStep = _route[_stepIndex];
            if (failedStep.mode == TravelMode::Hearthstone)
            {
                _hearthstoneBlocked = true;
                ++_hearthstoneFailureCount;
            }
            const TravelNode* fromNode =
                GetWorldGraph().GetNode(failedStep.fromNodeId);
            const TravelNode* toNode =
                GetWorldGraph().GetNode(failedStep.toNodeId);
            if (ShouldBlockFailedEdge(failedStep.edgeKey))
                _blockedEdges.insert(failedStep.edgeKey);
            // Static graph anchors are shared by many edges. Suppressing only
            // the failed edge can immediately route the bot back to the same
            // unreachable or unusable anchor through another connector.
            if (toNode)
                _blockedNodes.insert(failedStep.toNodeId);
            if (failedStep.mode != TravelMode::Walk &&
                fromNode)
                _blockedNodes.insert(failedStep.fromNodeId);

            if (bot && bot->GetTransport())
            {
                Transport* t = bot->GetTransport();
                if (t->GetDistance(failedStep.from.x, failedStep.from.y, failedStep.from.z) <= TransportDockRange ||
                    t->GetDistance(failedStep.to.x, failedStep.to.y, failedStep.to.z) <= TransportDockRange)
                {
                    t->RemovePassenger(bot);
                }
            }
        }
        else if (bot && bot->GetTransport())
        {
            bot->GetTransport()->RemovePassenger(bot);
        }
        if (movement)
            movement->Stop();

        ++_replanCount;
        _route.clear();
        _stepIndex = 0;
        _stepElapsedMs = 0;
        _walkPrePathWaitMs = 0;
        _walkProbeCooldownMs = 0;
        _walkCandidateCursor = 0;
        _walkProgress.Reset();
        _waitReason = TravelWaitReason::None;
        _stepPathBaselineSet = false;
        _transitionStarted = false;
        _mountAttempted = false;
        _failureReason = std::move(reason);

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_WARN("server", "[WorldBots] [Travel] Bot '{}' is replanning after travel edge failure ({}/{}) - {}",
                bot->GetName(), _replanCount, MaxReplans, _failureReason);
        }
        if (_replanCount >= MaxReplans)
            _failed = true;
    }

    TravelResult WorldTravel::UpdateStep(Player* bot, MovementManager* movement,
        uint32_t deltaMs, const std::vector<Brain::DangerArea>& dangerAreas,
        const std::vector<ObjectGuid>& nearbyHostileGuids)
    {
        if (_stepIndex >= _route.size())
            return IsNear(bot, _destination, StepArrivalRange) ? TravelResult::Arrived : TravelResult::InProgress;

        RouteStep const& step = _route[_stepIndex];
        RecordProgress(bot, step.to, deltaMs);
        _waitReason = TravelWaitReason::None;

        switch (step.mode)
        {
            case TravelMode::Walk:
            {
                _walkProbeCooldownMs = deltaMs >= _walkProbeCooldownMs
                    ? 0 : _walkProbeCooldownMs - deltaMs;
                if (!_stepPathBaselineSet)
                {
                    _stepPathAttemptGeneration = movement->GetPathAttemptGeneration();
                    _stepPathBaselineSet = true;
                }
                if (bot->GetMapId() != step.to.mapId)
                {
                    FailStep(bot, movement, "a walking segment unexpectedly changed maps");
                    break;
                }
                if (IsNear(bot, step.to, StepArrivalRange))
                {
                    movement->Stop();
                    CompleteStep(bot);
                    break;
                }
                if (!_mountAttempted && TravelGraph::Distance(step.from, step.to) >= 120.0f)
                {
                    _mountAttempted = true;
                    if (uint32_t mountSpell = FindUsableMountSpell(bot))
                    {
                        bot->CastSpell(bot, mountSpell, false);
                        _waitReason = TravelWaitReason::MountCast;
                        break;
                    }
                }
                if (bot->IsNonMeleeSpellCast(false))
                {
                    bool pathWasAttempted = movement->GetPathAttemptGeneration() !=
                        _stepPathAttemptGeneration;
                    if (!pathWasAttempted && !movement->HasPath())
                    {
                        _walkPrePathWaitMs = deltaMs >
                            std::numeric_limits<uint32_t>::max() - _walkPrePathWaitMs
                            ? std::numeric_limits<uint32_t>::max()
                            : _walkPrePathWaitMs + deltaMs;
                    }
                    else
                        _walkPrePathWaitMs = 0;

                    _waitReason = TravelWaitReason::Casting;
                    if (_walkPrePathWaitMs >= 8000)
                        FailStep(bot, movement,
                            "walking segment remained blocked by casting before any path request");
                    break;
                }
                _walkPrePathWaitMs = 0;
                bool moving = movement->HasPath();
                if (!moving && _walkProbeCooldownMs == 0)
                {
                    moving = StartSafeWalkingLeg(bot, movement, step,
                        dangerAreas, nearbyHostileGuids,
                        _visitedWalkFrontiers, _walkCandidateCursor);
                    if (!moving)
                        _walkProbeCooldownMs = WalkingProbeCooldownMs;
                }
                _waitReason = moving ? TravelWaitReason::Walking :
                    TravelWaitReason::PathRequest;
                uint32_t noProgressMs = _walkProgress.GetNoProgressMs();
                if ((!moving && noProgressMs >= 5000) || noProgressMs >= WalkNoProgressTimeoutMs ||
                    _stepElapsedMs >= WalkHardTimeoutMs)
                {
                    Common::PositionInfo failedEndpoint = BoundedLegEndpoint(
                        bot, step.to, TravelLegDistance);
                    float failedX = 0.0f, failedY = 0.0f, failedZ = 0.0f;
                    if (movement->GetLastPathAttemptEndpoint(
                        failedX, failedY, failedZ))
                    {
                        failedEndpoint = { failedX, failedY, failedZ,
                            bot->GetMapId() };
                    }
                    _failureArea = { bot->GetMapId(), failedEndpoint.x,
                        failedEndpoint.y, 45.0f, 0 };
                    _hasFailureArea = true;
                    FailStep(bot, movement,
                        "no safe short ground leg could reach the next travel waypoint");
                }
                break;
            }

            case TravelMode::FlightPath:
            {
                _waitReason = bot->IsInFlight() ? TravelWaitReason::Flight :
                    TravelWaitReason::FlightMaster;
                if (bot->IsInFlight())
                {
                    if (_stepElapsedMs >= FlightTimeoutMs)
                        FailStep(bot, movement, "flight path exceeded its transition timeout");
                    break;
                }
                if (_transitionStarted)
                {
                    if (IsNear(bot, step.to, 45.0f))
                        CompleteStep(bot);
                    else if (_stepElapsedMs >= 8000)
                        FailStep(bot, movement, "flight path ended away from the planned destination");
                    break;
                }
                if (!IsNear(bot, step.from, 45.0f))
                {
                    FailStep(bot, movement, "flight path source was not reached before activation");
                    break;
                }

                Creature* flightMaster = FindNearbyFlightMaster(bot, 55.0f);
                if (!flightMaster)
                {
                    if (_stepElapsedMs >= 12000)
                        FailStep(bot, movement, "no usable flight master appeared at the taxi node");
                    break;
                }
                bot->GetSession()->SendLearnNewTaxiNode(flightMaster);
                if (!bot->m_taxi.IsTaximaskNodeKnown(step.fromReference) ||
                    !bot->m_taxi.IsTaximaskNodeKnown(step.toReference))
                {
                    FailStep(bot, movement, "the planned flight path was not discovered");
                    break;
                }
                if (bot->IsMounted())
                    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                movement->Stop();
                std::vector<uint32> taxiRoute{ step.fromReference, step.toReference };
                bool started = bot->ActivateTaxiPathTo(taxiRoute, flightMaster);
                _transitionStarted = started || bot->IsInFlight() || IsNear(bot, step.to, TransitArrivalRange);
                if (!_transitionStarted && _stepElapsedMs >= 5000)
                    FailStep(bot, movement, "flight master rejected the planned taxi path");
                break;
            }

            case TravelMode::Transport:
            {
                _waitReason = TravelWaitReason::Transport;
                if (Transport* boarded = bot->GetTransport())
                {
                    if (bot->IsBeingTeleported())
                        Helper::TeleportUtils::CompletePendingTeleport(bot);

                    if (bot->GetMapId() == step.to.mapId &&
                        boarded->GetEntry() == step.reference &&
                        boarded->GetDistance(step.to.x, step.to.y, step.to.z) <= TransportDockRange)
                    {
                        boarded->RemovePassenger(bot);
                        CompleteStep(bot);
                    }
                    else if (_stepElapsedMs >= TransportTimeoutMs)
                        FailStep(bot, movement, "transport did not reach the planned stop before timeout");
                    break;
                }

                if (!IsNear(bot, step.from, TransportArrivalRange))
                {
                    FailStep(bot, movement, "transport boarding point was not reached");
                    break;
                }
                GameObject* gameObject = bot->FindNearestGameObject(step.reference, 90.0f);
                Transport* transport = gameObject ? gameObject->ToTransport() : nullptr;
                if (transport && transport->GetMapId() == bot->GetMapId() &&
                    transport->GetDistance(step.from.x, step.from.y, step.from.z) <= TransportDockRange)
                {
                    movement->Stop();
                    if (bot->IsMounted())
                        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                    float x = bot->GetPositionX();
                    float y = bot->GetPositionY();
                    float z = bot->GetPositionZ();
                    float orientation = bot->GetOrientation();
                    transport->CalculatePassengerOffset(x, y, z, &orientation);
                    bot->m_movementInfo.transport.pos.Relocate(x, y, z, orientation);
                    transport->AddPassenger(bot);
                    if (bot->GetTransport() == transport)
                    {
                        _transitionStarted = true;
                        _stepElapsedMs = 0;
                    }
                }
                if (!bot->GetTransport() && _stepElapsedMs >= TransportTimeoutMs)
                    FailStep(bot, movement, "transport never arrived or boarding failed");
                break;
            }

            case TravelMode::Portal:
            {
                _waitReason = TravelWaitReason::Portal;
                if (bot->IsBeingTeleported())
                    Helper::TeleportUtils::CompletePendingTeleport(bot);

                if (IsNear(bot, step.to, TransitArrivalRange))
                {
                    CompleteStep(bot);
                    break;
                }
                if (_transitionStarted)
                {
                    if (_stepElapsedMs >= PortalTimeoutMs)
                        FailStep(bot, movement, "portal use did not produce the expected transition");
                    break;
                }
                if (!IsNear(bot, step.from, 30.0f))
                {
                    FailStep(bot, movement, "portal source was not reached");
                    break;
                }
                GameObject* portal = bot->FindNearestGameObject(step.reference, 40.0f);
                if (portal && portal->isSpawned())
                {
                    movement->Stop();
                    if (bot->IsMounted() && !portal->GetGOInfo()->IsUsableMounted())
                        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                    portal->Use(bot);
                    _transitionStarted = true;
                }
                else if (_stepElapsedMs >= 10000)
                    FailStep(bot, movement, "portal game object was unavailable");
                break;
            }

            case TravelMode::Hearthstone:
            {
                _waitReason = TravelWaitReason::Hearthstone;
                if (IsNear(bot, step.to, TransitArrivalRange))
                {
                    CompleteStep(bot);
                    break;
                }
                if (_transitionStarted)
                {
                    if (bot->IsBeingTeleported())
                        Helper::TeleportUtils::CompletePendingTeleport(bot);

                    if (_stepElapsedMs > 500 && !bot->IsNonMeleeSpellCast(false) &&
                        !bot->IsBeingTeleported())
                    {
                        FailStep(bot, movement,
                            "hearthstone cast was interrupted or cancelled; disabling hearthstone for this journey");
                        break;
                    }

                    if (WorldTravelPolicy::HasHearthstoneTimedOut(
                        _stepElapsedMs))
                        FailStep(bot, movement,
                            "hearthstone cast did not reach the home bind; disabling hearthstone for this journey");
                    break;
                }
                Item* hearthstone = bot->GetItemByEntry(HearthstoneItemId);
                if (!hearthstone || bot->CanUseItem(hearthstone) != EQUIP_ERR_OK || bot->IsInCombat())
                {
                    FailStep(bot, movement, "hearthstone was missing, unusable, or on cooldown");
                    break;
                }
                movement->Stop();
                if (bot->IsMounted())
                    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                SpellCastTargets targets;
                targets.SetUnitTarget(bot);
                bot->CastItemUseSpell(hearthstone, targets, 0, 0);
                _transitionStarted = true;
                break;
            }
        }

        if (_failed)
            return TravelResult::Failed;
        if (IsNear(bot, _destination, StepArrivalRange))
        {
            _active = false;
            return TravelResult::Arrived;
        }
        if (_route.empty() && !BuildRoute(bot))
            return TravelResult::Failed;
        return TravelResult::InProgress;
    }
}
