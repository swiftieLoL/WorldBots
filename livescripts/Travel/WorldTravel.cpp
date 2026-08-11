#include "WorldTravel.h"

#include "Cache/BotCache.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DataStores/DBCStores.h"
#include "Diagnostics/BotTrace.h"
#include "Entities/Transport/Transport.h"
#include "GameObject.h"
#include "Globals/ObjectMgr.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Helper/MovementManager.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
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
        constexpr uint32_t WalkNoProgressTimeoutMs = 20000;
        constexpr uint32_t WalkHardTimeoutMs = 10 * 60 * 1000;
        constexpr uint32_t FlightTimeoutMs = 6 * 60 * 1000;
        constexpr uint32_t TransportTimeoutMs = 4 * 60 * 1000;
        constexpr uint32_t PortalTimeoutMs = 20000;
        constexpr uint32_t HearthTimeoutMs = 25000;
        constexpr uint32_t MaxReplans = 8;

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
                uint32_t destinationNode = graph.AddNode(destination,
                    TravelNodeKind::PortalDestination, portalSpellId);
                for (const Common::PositionInfo& sourcePosition : Cache::BotCache::GetGameObjectLocations(entry))
                {
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

        bool CanUseHearthstone(Player* bot)
        {
            if (!bot || bot->IsInCombat() || bot->m_homebindMapId == UINT32_MAX)
                return false;
            Item* hearthstone = bot->GetItemByEntry(HearthstoneItemId);
            return hearthstone && bot->CanUseItem(hearthstone) == EQUIP_ERR_OK;
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

    void WorldTravel::Reset()
    {
        _route.clear();
        _stepIndex = 0;
        _blockedEdges.clear();
        _stepElapsedMs = 0;
        _noProgressMs = 0;
        _replanCount = 0;
        _hasProgressPosition = false;
        _transitionStarted = false;
        _mountAttempted = false;
        _active = false;
        _failed = false;
        _failureReason.clear();
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

    bool WorldTravel::BuildRoute(Player* bot)
    {
        RouteOptions options;
        for (const TravelNode& node : GetWorldGraph().GetNodes())
        {
            if (node.kind == TravelNodeKind::FlightMaster &&
                bot->m_taxi.IsTaximaskNodeKnown(node.reference))
            {
                options.knownTaxiNodes.insert(node.reference);
            }
        }
        options.blockedEdges = _blockedEdges;
        options.canUseHearthstone = CanUseHearthstone(bot);
        options.home = { bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ, bot->m_homebindMapId };

        Common::PositionInfo start{ bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId() };
        _route = GetWorldGraph().FindRoute(start, _destination, options);
        _stepIndex = 0;
        _stepElapsedMs = 0;
        _noProgressMs = 0;
        _hasProgressPosition = false;
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
        Common::PositionInfo destination, uint32_t deltaMs)
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
            _active = true;
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

        return UpdateStep(bot, movement, deltaMs);
    }

    void WorldTravel::RecordProgress(Player* bot, uint32_t deltaMs)
    {
        _stepElapsedMs += deltaMs;
        if (!_hasProgressPosition || _lastMapId != bot->GetMapId())
        {
            _lastMapId = bot->GetMapId();
            _lastX = bot->GetPositionX();
            _lastY = bot->GetPositionY();
            _lastZ = bot->GetPositionZ();
            _hasProgressPosition = true;
            _noProgressMs = 0;
            return;
        }

        float dx = bot->GetPositionX() - _lastX;
        float dy = bot->GetPositionY() - _lastY;
        float dz = bot->GetPositionZ() - _lastZ;
        if (dx * dx + dy * dy + dz * dz >= 1.0f)
        {
            _lastX = bot->GetPositionX();
            _lastY = bot->GetPositionY();
            _lastZ = bot->GetPositionZ();
            _noProgressMs = 0;
        }
        else
            _noProgressMs += deltaMs;
    }

    void WorldTravel::CompleteStep(Player* bot)
    {
        DiscoverNearbyFlightPath(bot);
        ++_stepIndex;
        _stepElapsedMs = 0;
        _noProgressMs = 0;
        _hasProgressPosition = false;
        _transitionStarted = false;
        _mountAttempted = false;
        if (_stepIndex >= _route.size())
            _route.clear();
    }

    void WorldTravel::FailStep(Player* bot, MovementManager* movement, std::string reason)
    {
        if (_stepIndex < _route.size())
            _blockedEdges.insert(_route[_stepIndex].edgeKey);
        if (bot && bot->GetTransport())
            bot->GetTransport()->RemovePassenger(bot);
        if (movement)
            movement->Stop();

        ++_replanCount;
        _route.clear();
        _stepIndex = 0;
        _stepElapsedMs = 0;
        _noProgressMs = 0;
        _hasProgressPosition = false;
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

    TravelResult WorldTravel::UpdateStep(Player* bot, MovementManager* movement, uint32_t deltaMs)
    {
        if (_stepIndex >= _route.size())
            return IsNear(bot, _destination, StepArrivalRange) ? TravelResult::Arrived : TravelResult::InProgress;

        RouteStep const& step = _route[_stepIndex];
        RecordProgress(bot, deltaMs);

        switch (step.mode)
        {
            case TravelMode::Walk:
            {
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
                        break;
                    }
                }
                if (bot->IsNonMeleeSpellCast(false))
                    break;
                bool moving = movement->MoveTo(step.to.x, step.to.y, step.to.z,
                    BotMovementState::Moving, false);
                if ((!moving && _noProgressMs >= 5000) || _noProgressMs >= WalkNoProgressTimeoutMs ||
                    _stepElapsedMs >= WalkHardTimeoutMs)
                {
                    FailStep(bot, movement, "ground routing could not reach the next travel waypoint");
                }
                break;
            }

            case TravelMode::FlightPath:
            {
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
                _transitionStarted = started || bot->IsInFlight() || IsNear(bot, step.to, 45.0f);
                if (!_transitionStarted && _stepElapsedMs >= 5000)
                    FailStep(bot, movement, "flight master rejected the planned taxi path");
                break;
            }

            case TravelMode::Transport:
            {
                if (Transport* boarded = bot->GetTransport())
                {
                    if (bot->GetMapId() == step.to.mapId &&
                        boarded->GetEntry() == step.reference &&
                        boarded->GetDistance(step.to.x, step.to.y, step.to.z) <= TransportArrivalRange)
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
                    transport->GetDistance(step.from.x, step.from.y, step.from.z) <= TransportArrivalRange)
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
                    _transitionStarted = bot->GetTransport() == transport;
                }
                if (!bot->GetTransport() && _stepElapsedMs >= TransportTimeoutMs)
                    FailStep(bot, movement, "transport never arrived or boarding failed");
                break;
            }

            case TravelMode::Portal:
            {
                if (IsNear(bot, step.to, 45.0f))
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
                if (IsNear(bot, step.to, 45.0f))
                {
                    CompleteStep(bot);
                    break;
                }
                if (_transitionStarted)
                {
                    if (_stepElapsedMs >= HearthTimeoutMs)
                        FailStep(bot, movement, "hearthstone cast did not reach the home bind");
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
