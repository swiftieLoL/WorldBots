#include "GrindAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"

#include "Blackboard/BotBlackboard.h"
#include "Cache/BotCache.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "Creature.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Helper/MathUtils.h"
#include "Helper/MovementPathPolicy.h"
#include "Helper/CombatUtils.h"
#include "Helper/Constants.h"
#include "Helper/GrindFallbackPolicy.h"
#include "Helper/ProgressionPolicy.h"
#include "Helper/TeleportUtils.h"
#include "Helper/TimeUtils.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Party/PartyCombat.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace Actions
{
    GrindAction::GrindAction(int32_t minLevelOffset, int32_t maxLevelOffset,
        std::unordered_map<uint64_t, uint32_t> suppressedSpawnIds,
        std::unordered_map<uint32_t, uint32_t> suppressedCreatureEntries,
        std::vector<Brain::DestinationSuppression> suppressedDestinations,
        std::vector<Brain::DangerArea> dangerAreas)
        : _minLevelOffset(minLevelOffset), _maxLevelOffset(maxLevelOffset),
          _suppressedSpawnIds(std::move(suppressedSpawnIds)),
          _suppressedCreatureEntries(std::move(suppressedCreatureEntries)),
          _suppressedDestinations(std::move(suppressedDestinations)),
          _dangerAreas(std::move(dangerAreas))
    {
    }

    void GrindAction::Start(Player* bot, MovementManager* /*movement*/)
    {
        _targetGuid.Clear();
        _targetEntry = 0;
        _huntingDestinationEntry = 0;
        _targetSearchCooldownMs = 0;
        _destinationRefreshMs = 0;
        _unproductiveMs = 0;
        _unreachableAnchorCount = 0;
        _liveAnchorMissCount = 0;
        _huntingDestinationSpawnId = 0;
        _huntingDestinationX = 0.0f;
        _huntingDestinationY = 0.0f;
        _huntingDestinationZ = 0.0f;
        _hasHuntingDestination = false;
        _areaRelocationAttempted = false;
        _worldTravel.Reset();
        _liveCandidatesSeen = 0;
        _liveInvalid = 0;
        _liveLevelOrRank = 0;
        _liveSuppressed = 0;
        _liveDangerArea = 0;
        _liveUnsafePack = 0;
        _liveEligible = 0;
        _suppressedLiveTargetGuids.clear();
        _liveTargetPathFailures.Reset();
        _liveTargetApproachProgress.Reset();
        _combatProgressWatchdog.Reset();
        _huntingTravelProgress.Reset();
        _anchorSearchDiagnostics = "not_searched";
        uint32_t nowSec = Helper::MonotonicSeconds();
        _suppressedDestinations.erase(std::remove_if(
            _suppressedDestinations.begin(), _suppressedDestinations.end(),
            [nowSec](const Brain::DestinationSuppression& destination) {
                return destination.untilSec <= nowSec;
            }), _suppressedDestinations.end());
        RefreshCandidateDiagnostics();
        ResetOutcome();
        if (bot)
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
    }

    bool GrindAction::IsTargetSuppressed(Creature* creature) const
    {
        if (!creature)
            return false;
        uint32_t nowSec = Helper::MonotonicSeconds();
        auto guidIt = _suppressedLiveTargetGuids.find(
            creature->GetGUID().GetRawValue());
        if (guidIt != _suppressedLiveTargetGuids.end() &&
            nowSec < guidIt->second)
        {
            return true;
        }
        auto entryIt = _suppressedCreatureEntries.find(creature->GetEntry());
        if (entryIt != _suppressedCreatureEntries.end() && nowSec < entryIt->second)
            return true;
        auto spawnIt = _suppressedSpawnIds.find(creature->GetSpawnId());
        return spawnIt != _suppressedSpawnIds.end() && nowSec < spawnIt->second;
    }

    bool GrindAction::IsSafeTarget(Player* bot, Creature* creature) const
    {
        if (Helper::CombatUtils::ValidateTarget(bot, creature) !=
                Helper::CombatUtils::TargetValidationResult::Valid ||
            creature->IsCritter() || creature->IsCivilian())
            return false;

        CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate();
        if (!creatureTemplate)
            return false;

        bool isAttackingBot = creature->GetVictim() == bot || creature->IsInCombatWith(bot);
        if (!isAttackingBot)
        {
            if (creatureTemplate->rank != CREATURE_ELITE_NORMAL ||
                !Helper::IsGrindingLevelSuitable(bot->GetLevel(), creature->GetLevel(),
                    _minLevelOffset, _maxLevelOffset))
                return false;
        }

        return true;
    }

    bool GrindAction::HasPotentialAdd(Player* bot, Creature* target,
        const Blackboard::BotBlackboard& blackboard) const
    {
        if (!bot || !target)
            return false;

        constexpr float UnsafePullSeparation = 15.0f;
        for (ObjectGuid guid : blackboard.spatial.hostileGuids)
        {
            if (guid == target->GetGUID())
                continue;
            Creature* nearby = ObjectAccessor::GetCreature(*bot, guid);
            if (!nearby || !nearby->IsAlive() || nearby->GetMap() != bot->GetMap() ||
                nearby->IsCritter() || nearby->IsCivilian() ||
                !nearby->isTargetableForAttack() || !bot->IsValidAttackTarget(nearby))
                continue;

            // A creature already fighting another player is not part of this
            // pull. An unclaimed hostile close to the intended target is a
            // likely add and makes the proactive grind target unsafe.
            if (Unit* victim = nearby->GetVictim(); victim && victim != bot)
                continue;
            if (target->GetDistance(nearby) <= UnsafePullSeparation)
                return true;
        }
        return false;
    }

    bool GrindAction::IsInsideDangerArea(uint32_t mapId, float x, float y) const
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        return std::any_of(_dangerAreas.begin(), _dangerAreas.end(),
            [=](const Brain::DangerArea& area) {
                return area.Contains(mapId, x, y, nowSec);
            });
    }

    bool GrindAction::RouteCrossesDangerArea(uint32_t mapId, float fromX, float fromY,
        float toX, float toY) const
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        return std::any_of(_dangerAreas.begin(), _dangerAreas.end(),
            [=](const Brain::DangerArea& area) {
                return area.BlocksTravelSegment(mapId, fromX, fromY,
                    toX, toY, nowSec);
            });
    }

    Creature* GrindAction::SelectTarget(Player* bot, const Blackboard::BotBlackboard& blackboard)
    {
        _liveCandidatesSeen = 0;
        _liveInvalid = 0;
        _liveLevelOrRank = 0;
        _liveSuppressed = 0;
        _liveDangerArea = 0;
        _liveUnsafePack = 0;
        _liveEligible = 0;
        Creature* bestTarget = nullptr;
        float bestScore = std::numeric_limits<float>::max();
        for (ObjectGuid guid : blackboard.spatial.hostileGuids)
        {
            ++_liveCandidatesSeen;
            Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (Helper::CombatUtils::ValidateTarget(bot, creature) !=
                    Helper::CombatUtils::TargetValidationResult::Valid ||
                !creature || creature->IsCritter() || creature->IsCivilian())
            {
                ++_liveInvalid;
                continue;
            }

            CreatureTemplate const* creatureTemplate =
                creature->GetCreatureTemplate();
            if (!creatureTemplate)
            {
                ++_liveInvalid;
                continue;
            }
            bool defendingSelf = creature->GetVictim() == bot ||
                creature->IsInCombatWith(bot);
            if (!defendingSelf &&
                (creatureTemplate->rank != CREATURE_ELITE_NORMAL ||
                 !Helper::IsGrindingLevelSuitable(bot->GetLevel(),
                    creature->GetLevel(), _minLevelOffset, _maxLevelOffset)))
            {
                ++_liveLevelOrRank;
                continue;
            }
            // Never deliberately reacquire a spawn that recently killed this
            // bot. If it attacks again, self-defence still takes precedence.
            if (IsTargetSuppressed(creature) && creature->GetVictim() != bot &&
                !creature->IsInCombatWith(bot))
            {
                ++_liveSuppressed;
                continue;
            }

            if (!defendingSelf && IsInsideDangerArea(creature->GetMapId(),
                creature->GetPositionX(), creature->GetPositionY()))
            {
                ++_liveDangerArea;
                continue;
            }
            if (!defendingSelf && HasPotentialAdd(bot, creature, blackboard))
            {
                ++_liveUnsafePack;
                // Keep this decision local to the current action. The spawn
                // may become safe after creatures roam apart, so a short
                // suppression is enough to select another hunting position.
                if (uint64_t spawnId = creature->GetSpawnId(); spawnId != 0)
                    _suppressedSpawnIds[spawnId] = Helper::MonotonicSeconds() + 60;
                continue;
            }

            ++_liveEligible;

            // Prefer the safest useful enemy first, then distance. This keeps
            // the fallback conservative while still awarding non-trivial XP.
            float levelPenalty = static_cast<float>(creature->GetLevel()) * 100.0f;
            float score = levelPenalty + bot->GetDistance(creature);
            if (score < bestScore)
            {
                bestScore = score;
                bestTarget = creature;
            }
        }
        RefreshCandidateDiagnostics();
        return bestTarget;
    }

    void GrindAction::RefreshCandidateDiagnostics()
    {
        std::ostringstream detail;
        detail << "live_seen=" << _liveCandidatesSeen
               << ",invalid=" << _liveInvalid
               << ",level_or_rank=" << _liveLevelOrRank
               << ",suppressed=" << _liveSuppressed
               << ",danger=" << _liveDangerArea
               << ",unsafe_pack=" << _liveUnsafePack
               << ",eligible=" << _liveEligible
               << ";unreachable_anchors=" << _unreachableAnchorCount
               << ";anchor=" << _anchorSearchDiagnostics;
        _candidateDiagnostics = detail.str();
    }

    bool GrindAction::HandleRepeatedLiveTargetPathFailure(Player* bot,
        Creature* target, MovementManager* movement,
        uint64_t pathGenerationBefore)
    {
        if (!bot || !target || !movement)
            return false;

        bool freshAttempt = movement->GetPathAttemptGeneration() !=
            pathGenerationBefore;
        bool failed = movement->GetLastPathFailure() != BotPathFailure::None;
        uint64_t targetKey = target->GetGUID().GetRawValue();
        if (!_liveTargetPathFailures.Observe(targetKey, freshAttempt, failed))
            return false;

        // A hostile already engaged with the bot remains self-defence. Only a
        // proactive target can be replaced safely.
        if (target->GetVictim() == bot || target->IsInCombatWith(bot))
            return false;

        constexpr uint32_t LiveTargetSuppressionSeconds = 60;
        _suppressedLiveTargetGuids[targetKey] =
            Helper::MonotonicSeconds() + LiveTargetSuppressionSeconds;
        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' skipped live target '{}' (Entry {}, GUID {}) for {} seconds after {} fresh path failures: {} (flags {})",
                bot->GetName(), target->GetName(), target->GetEntry(),
                target->GetGUID().GetCounter(), LiveTargetSuppressionSeconds,
                _liveTargetPathFailures.GetFailureCount(),
                movement->GetLastPathFailureName(),
                movement->GetLastPathFlags());
        }
        movement->Stop();
        _targetGuid.Clear();
        _targetEntry = 0;
        _targetSearchCooldownMs = 0;
        _liveTargetPathFailures.Reset();
        return true;
    }

    bool GrindAction::HandleLiveTargetApproachStall(Player* bot,
        Creature* target, MovementManager* movement,
        uint64_t pathGenerationBefore, uint32_t deltaMs)
    {
        if (!bot || !target || !movement)
            return false;

        bool freshAttempt = movement->GetPathAttemptGeneration() !=
            pathGenerationBefore;
        uint64_t targetKey = target->GetGUID().GetRawValue();
        bool engaged = target->GetVictim() == bot ||
            target->IsInCombatWith(bot);
        if (!_liveTargetApproachProgress.Observe(targetKey,
            bot->GetDistance(target), target->GetHealth(), engaged,
            freshAttempt, deltaMs))
        {
            return false;
        }

        if (engaged)
            return false;

        constexpr uint32_t LiveTargetSuppressionSeconds = 60;
        uint32_t stalledMs = _liveTargetApproachProgress.GetNoProgressMs();
        uint32_t pathAttempts =
            _liveTargetApproachProgress.GetPathAttemptCount();
        _suppressedLiveTargetGuids[targetKey] =
            Helper::MonotonicSeconds() + LiveTargetSuppressionSeconds;

        Diagnostics::StructuredEvent event;
        event.event = "grind_live_target_suppressed";
        event.goal = "Grind";
        event.action = GetName();
        event.requestX = target->GetPositionX();
        event.requestY = target->GetPositionY();
        event.requestZ = target->GetPositionZ();
        event.pathFailure = movement->GetLastPathFailureName();
        event.pathFlags = movement->GetLastPathFlags();
        event.pathAttemptGeneration = movement->GetPathAttemptGeneration();
        std::ostringstream details;
        details << "reason=approach_no_progress;entry="
            << target->GetEntry() << ";guid="
            << target->GetGUID().GetCounter() << ";stalled_ms="
            << stalledMs << ";path_attempts=" << pathAttempts
            << ";distance=" << bot->GetDistance(target)
            << ";target_health=" << target->GetHealth();
        event.details = details.str();
        Diagnostics::StructuredEventLog::Write(bot, std::move(event));

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' skipped live target '{}' (Entry {}, GUID {}) for {} seconds after {}ms and {} path attempts produced no damage, combat engagement, or meaningful distance progress",
                bot->GetName(), target->GetName(), target->GetEntry(),
                target->GetGUID().GetCounter(),
                LiveTargetSuppressionSeconds, stalledMs, pathAttempts);
        }
        movement->Stop();
        _targetGuid.Clear();
        _targetEntry = 0;
        _targetSearchCooldownMs = 0;
        _liveTargetPathFailures.Reset();
        _liveTargetApproachProgress.Reset();
        return true;
    }

    bool GrindAction::TravelToHuntingGround(Player* bot, MovementManager* movement)
    {
        if (!bot || !movement || movement->GetState() != BotMovementState::Idle)
            return false;

        Cache::PositionInfo destination;
        uint32_t creatureEntry = 0;
        uint64_t spawnId = 0;
        std::unordered_set<uint64_t> suppressedSpawnIds;
        std::unordered_set<uint32_t> suppressedCreatureEntries;
        uint32_t nowSec = Helper::MonotonicSeconds();
        for (const auto& [spawnId, expirySec] : _suppressedSpawnIds)
            if (nowSec < expirySec)
                suppressedSpawnIds.insert(spawnId);
        for (const auto& [entry, expirySec] : _suppressedCreatureEntries)
            if (nowSec < expirySec)
                suppressedCreatureEntries.insert(entry);
        auto safePosition = [this, bot, nowSec](const Cache::PositionInfo& candidate) {
                bool destinationSuppressed = std::any_of(
                    _suppressedDestinations.begin(),
                    _suppressedDestinations.end(),
                    [&](const Brain::DestinationSuppression& suppression) {
                        return suppression.Contains(candidate.mapId,
                            candidate.x, candidate.y, nowSec);
                    });
                return !destinationSuppressed &&
                    !IsInsideDangerArea(candidate.mapId, candidate.x, candidate.y) &&
                    !RouteCrossesDangerArea(candidate.mapId,
                        bot->GetPositionX(), bot->GetPositionY(), candidate.x, candidate.y);
            };

        bool relocatingArea = Helper::GrindFallbackPolicy::ShouldRelocateArea(
            _unproductiveMs);
        bool selectedViableArea = false;
        bool found = false;
        Cache::GrindingSearchDiagnostics areaDiagnostics;
        Cache::GrindingSearchDiagnostics localDiagnostics;
        if (relocatingArea)
        {
            found = Cache::BotCache::FindViableGrindingArea(bot,
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                bot->GetMapId(), bot->GetZoneId(), Constants::MaxFleeDistance,
                _minLevelOffset, _maxLevelOffset, destination, creatureEntry, spawnId,
                suppressedSpawnIds, suppressedCreatureEntries, safePosition,
                &areaDiagnostics);
            _areaRelocationAttempted = true;
            selectedViableArea = found;
        }

        if (!found)
        {
            float localRadius = Helper::GrindFallbackPolicy::GetLocalSearchRadius(
                _unproductiveMs);
            found = Cache::BotCache::FindNearestGrindingCreature(bot,
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                bot->GetMapId(), Constants::MaxFleeDistance, _minLevelOffset,
                _maxLevelOffset, destination, creatureEntry, suppressedSpawnIds,
                suppressedCreatureEntries, safePosition, localRadius, &spawnId,
                &localDiagnostics);
        }

        std::ostringstream anchorDetail;
        anchorDetail << (found ? (selectedViableArea
                ? "viable_selected" : "local_selected") : "not_found");
        auto appendSearchDiagnostics = [&anchorDetail](const char* label,
            const Cache::GrindingSearchDiagnostics& diagnostics) {
            anchorDetail << ';' << label
                << "(indexed=" << diagnostics.indexedCandidates
                << ",suppressed=" << diagnostics.suppressed
                << ",event=" << diagnostics.inactiveEvent
                << ",non_hostile=" << diagnostics.nonHostile
                << ",hazard=" << diagnostics.unsafePosition
                << ",level_fit=" << diagnostics.weakLevelFit
                << ",near=" << diagnostics.tooNear
                << ",far=" << diagnostics.tooFar
                << ",eligible=" << diagnostics.eligibleInRange
                << ",cells=" << diagnostics.viableCells << ')';
        };
        if (relocatingArea)
            appendSearchDiagnostics("area", areaDiagnostics);
        if (!selectedViableArea)
            appendSearchDiagnostics("local", localDiagnostics);
        _anchorSearchDiagnostics = anchorDetail.str();
        RefreshCandidateDiagnostics();

        if (!found)
        {
            // Avoid rescanning every creature spawn on every brain tick.
            _destinationRefreshMs = 1000;
            return false;
        }

        // The DB-backed ecology only nominated this area. Resolve it against
        // the loaded map before committing to travel so phased, inactive,
        // pooled-out, dead, scripted-away, or otherwise untargetable spawns do
        // not produce a successful journey to an empty hunting ground.
        Cache::PositionInfo liveDestination;
        uint32_t liveCreatureEntry = 0;
        uint64_t liveSpawnId = 0;
        constexpr float LocalLiveAnchorRadius = 120.0f;
        constexpr float AreaLiveAnchorRadius = 300.0f;
        float liveAnchorRadius = selectedViableArea
            ? AreaLiveAnchorRadius : LocalLiveAnchorRadius;
        if (!Cache::BotCache::ResolveLiveGrindingAnchor(bot, destination,
            liveAnchorRadius, _minLevelOffset, _maxLevelOffset,
            liveDestination, liveCreatureEntry, liveSpawnId,
            suppressedSpawnIds, suppressedCreatureEntries, safePosition))
        {
            uint32_t expirySec = Helper::MonotonicSeconds() +
                Helper::GrindFallbackPolicy::AbsentAnchorSuppressionSeconds;
            if (spawnId != 0)
                _suppressedSpawnIds[spawnId] = expirySec;
            _suppressedDestinations.push_back({ destination.mapId,
                destination.x, destination.y, liveAnchorRadius, expirySec });
            if (_suppressedDestinations.size() >
                Brain::SuppressionRegistry::MaxGrindDestinations)
            {
                _suppressedDestinations.erase(
                    _suppressedDestinations.begin());
            }

            _anchorSearchDiagnostics +=
                ",live_preflight_rejected=no eligible live phased target";
            ++_liveAnchorMissCount;
            _destinationRefreshMs = 0;
            RefreshCandidateDiagnostics();

            Diagnostics::StructuredEvent event;
            event.event = "grind_anchor_live_preflight_failed";
            event.goal = "Grind";
            event.action = GetName();
            event.requestX = destination.x;
            event.requestY = destination.y;
            event.requestZ = destination.z;
            std::ostringstream details;
            details << "entry=" << creatureEntry << ";spawn=" << spawnId
                << ";radius=" << liveAnchorRadius
                << ";reason=no eligible live phased target"
                << ";missed_anchors=" << _liveAnchorMissCount;
            event.details = details.str();
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));

            if (Helper::GrindFallbackPolicy::ShouldEndAfterAbsentAnchorMisses(
                _liveAnchorMissCount))
            {
                Finish(ActionOutcome::RetryableFailure,
                    "ten distinct hunting anchors lacked eligible live targets",
                    FailureCategory::ProgressionDifficulty,
                    RecoveryDirective::RetryLater);
            }
            return true;
        }

        destination = liveDestination;
        creatureEntry = liveCreatureEntry;
        spawnId = liveSpawnId;
        _liveAnchorMissCount = 0;
        _anchorSearchDiagnostics += ",live_preflight=eligible";

        // Preserve the spawn's authored layer and let PathGenerator resolve
        // the navmesh height. VMAP GetHeight can select a roof, canopy, or a
        // lower interior surface at the same XY in layered terrain.
        float targetZ = destination.z;

        auto rejectAnchor = [&](const char* reason) {
            uint32_t expirySec = Helper::MonotonicSeconds() +
                Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
            if (spawnId != 0)
                _suppressedSpawnIds[spawnId] = expirySec;
            else if (creatureEntry != 0)
                _suppressedCreatureEntries[creatureEntry] = expirySec;
            _suppressedDestinations.push_back({ destination.mapId,
                destination.x, destination.y, 30.0f, expirySec });
            if (_suppressedDestinations.size() >
                Brain::SuppressionRegistry::MaxGrindDestinations)
            {
                _suppressedDestinations.erase(
                    _suppressedDestinations.begin());
            }

            _anchorSearchDiagnostics += std::string(",preflight_rejected=") +
                reason;
            ++_unreachableAnchorCount;
            _destinationRefreshMs = 0;
            RefreshCandidateDiagnostics();

            Diagnostics::StructuredEvent event;
            event.event = "grind_anchor_preflight_failed";
            event.goal = "Grind";
            event.action = GetName();
            event.requestX = destination.x;
            event.requestY = destination.y;
            event.requestZ = targetZ;
            std::ostringstream details;
            details << "entry=" << creatureEntry << ";spawn=" << spawnId
                << ";reason=" << reason << ";unreachable_anchors="
                << _unreachableAnchorCount << ";suppression_s="
                << Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
            event.details = details.str();
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));

            if (Helper::GrindFallbackPolicy::ShouldEndAfterUnreachableAnchor(
                _unreachableAnchorCount))
            {
                Finish(ActionOutcome::RetryableFailure,
                    "three distinct hunting anchors failed live/path preflight",
                    FailureCategory::ProgressionDifficulty,
                    RecoveryDirective::RetryLater);
            }
        };

        // Static ecology anchors are not live tactical targets. Prove a full
        // corridor for local anchors. Remote anchors exceed Trinity's fixed
        // complete-path budget, so MovementManager must instead prove each
        // bounded executable leg and report any later frontier failure.
        float anchorDistance = Helper::Distance2D(bot->GetPositionX(),
            bot->GetPositionY(), destination.x, destination.y);
        bool requiresCompletePreflight =
            Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(
                anchorDistance);

        Common::PositionInfo destPos{ destination.x, destination.y, targetZ, destination.mapId };
        bool needsWorldTravel = Travel::WorldTravel::NeedsTravel(bot, destPos,
            Helper::GrindFallbackPolicy::CompletePathPreflightRadius);

        if (needsWorldTravel)
        {
            if (!Travel::WorldTravel::CanReach(bot, destPos))
            {
                rejectAnchor("unreachable via world travel graph");
                return true;
            }

            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' beginning WorldTravel toward distant hunting entry {} at ({:.1f}, {:.1f}, {:.1f}) Map {} [{:.0f}yd away]",
                    bot->GetName(), creatureEntry, destination.x, destination.y, targetZ, destination.mapId, anchorDistance);
            }

            _huntingDestinationEntry = creatureEntry;
            _huntingDestinationSpawnId = spawnId;
            _huntingDestinationX = destination.x;
            _huntingDestinationY = destination.y;
            _huntingDestinationZ = targetZ;
            _hasHuntingDestination = true;
            _huntingTravelProgress.Reset();
            _destinationRefreshMs = 15000;
            return true;
        }

        if (requiresCompletePreflight &&
            Helper::TeleportUtils::HasUsableGroundOrigin(bot) &&
            !Helper::TeleportUtils::HasCompleteGroundPathTo(bot,
                destination.x, destination.y, targetZ))
        {
            rejectAnchor("no complete ground path");
            return true;
        }

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' {} toward safe hunting entry {} at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away, {}ms without target]",
                bot->GetName(), selectedViableArea ? "relocating to a viable hunting area" : "expanding the local hunting area",
                creatureEntry, destination.x, destination.y, targetZ,
                anchorDistance,
                _unproductiveMs);
        }

        {
            std::ostringstream pathSource;
            pathSource << "grind_anchor;selection="
                << (selectedViableArea ? "viable_area" : "local")
                << ";entry=" << creatureEntry
                << ";spawn=" << spawnId
                << ";complete_preflight=" << requiresCompletePreflight
                << ";unreachable_anchors=" << _unreachableAnchorCount;
            movement->SetDiagnosticPathSource(pathSource.str());
        }
        if (!movement->MoveTo(destination.x, destination.y, targetZ,
            BotMovementState::Moving, false))
        {
            _anchorSearchDiagnostics += std::string(",path_rejected=") +
                movement->GetLastPathFailureName();
            RefreshCandidateDiagnostics();
            _destinationRefreshMs = 1000;
            if ((movement->GetLastPathFlags() &
                Helper::MovementPathPolicy::FarFromPolyStart) != 0)
            {
                // Every destination will fail while the bot's origin is off
                // the navmesh. Keep the candidate innocent and let the shared
                // origin circuit breaker recover the bot after bounded proof.
                return true;
            }
            rejectAnchor(movement->GetLastPathFailureName());
            return true;
        }

        // Only a path-validated anchor becomes the active hunting destination.
        _huntingDestinationEntry = creatureEntry;
        _huntingDestinationSpawnId = spawnId;
        _huntingDestinationX = destination.x;
        _huntingDestinationY = destination.y;
        _huntingDestinationZ = targetZ;
        _hasHuntingDestination = true;
        _huntingTravelProgress.Reset();
        if (requiresCompletePreflight)
            _unreachableAnchorCount = 0;
        _destinationRefreshMs = 15000;
        return true;
    }

    void GrindAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !movement || !bot->IsInWorld() || !bot->IsAlive())
            return;
        if (_completed)
            return;

        _targetSearchCooldownMs = deltaMs >= _targetSearchCooldownMs ? 0 : _targetSearchCooldownMs - deltaMs;
        _destinationRefreshMs = deltaMs >= _destinationRefreshMs ? 0 : _destinationRefreshMs - deltaMs;

        Creature* target = _targetGuid ? ObjectAccessor::GetCreature(*bot, _targetGuid) : nullptr;
        if (!IsSafeTarget(bot, target))
        {
            if (target && !target->IsAlive() && bot->GetVictim() == target)
                bot->AttackStop();
            _targetGuid.Clear();
            _targetEntry = 0;
            _combatProgressWatchdog.Reset();
            target = nullptr;
        }

        if (!target && _targetSearchCooldownMs == 0)
        {
            _targetSearchCooldownMs = 500;
            target = SelectTarget(bot, blackboard);
            if (target)
            {
                _worldTravel.Reset();
                _unproductiveMs = 0;
                _unreachableAnchorCount = 0;
                _huntingDestinationEntry = 0;
                _huntingDestinationSpawnId = 0;
                _hasHuntingDestination = false;
                _targetGuid = target->GetGUID();
                _targetEntry = target->GetEntry();
                _combatProgressWatchdog.Reset();
                _huntingTravelProgress.Reset();
                movement->Stop();
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' hunting {} (Entry {}, Level {})",
                        bot->GetName(), target->GetName(), target->GetEntry(), target->GetLevel());
            }
        }

        if (target)
        {
            _unproductiveMs = 0;
            bool targetEngagedWithBot = target->GetVictim() == bot ||
                target->IsInCombatWith(bot);
            if (_combatProgressWatchdog.UpdateWhileEngaged(
                bot->IsInCombat() && targetEngagedWithBot,
                bot->GetHealth(), target->GetHealth(), deltaMs))
            {
                RecoverFromNoDamageStall(bot, target, movement);
                return;
            }
            if (!_classStrategy)
                _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
            std::ostringstream pathSource;
            pathSource << "grind_live_target;entry=" << target->GetEntry()
                << ";guid=" << target->GetGUID().GetCounter();
            movement->SetDiagnosticPathSource(pathSource.str());
            uint64_t pathGeneration = movement->GetPathAttemptGeneration();
            bool roleHandled = Party::HandleRoleAction(bot, target, movement,
                blackboard);
            if (!roleHandled && _classStrategy)
                _classStrategy->UpdateCombat(bot, target, movement, blackboard, deltaMs);
            if (HandleRepeatedLiveTargetPathFailure(bot, target, movement,
                pathGeneration))
            {
                return;
            }
            if (HandleLiveTargetApproachStall(bot, target, movement,
                pathGeneration, deltaMs))
            {
                return;
            }
            return;
        }

        _unproductiveMs = deltaMs > std::numeric_limits<uint32_t>::max() - _unproductiveMs
            ? std::numeric_limits<uint32_t>::max() : _unproductiveMs + deltaMs;

        if (_hasHuntingDestination)
        {
            Common::PositionInfo destPos{ _huntingDestinationX, _huntingDestinationY,
                _huntingDestinationZ, bot->GetMapId() };
            if (_worldTravel.IsActive() ||
                Travel::WorldTravel::NeedsTravel(bot, destPos,
                    Helper::GrindFallbackPolicy::CompletePathPreflightRadius))
            {
                Travel::TravelResult travelResult = _worldTravel.Update(bot, movement,
                    destPos, deltaMs, _dangerAreas,
                    blackboard.spatial.hostileGuids);
                if (travelResult == Travel::TravelResult::Failed)
                {
                    uint32_t expirySec = Helper::MonotonicSeconds() +
                        Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
                    if (_huntingDestinationSpawnId != 0)
                        _suppressedSpawnIds[_huntingDestinationSpawnId] = expirySec;
                    else if (_huntingDestinationEntry != 0)
                        _suppressedCreatureEntries[_huntingDestinationEntry] = expirySec;
                    _suppressedDestinations.push_back({ bot->GetMapId(),
                        _huntingDestinationX, _huntingDestinationY, 30.0f, expirySec });
                    if (_suppressedDestinations.size() > Brain::SuppressionRegistry::MaxGrindDestinations)
                        _suppressedDestinations.erase(_suppressedDestinations.begin());

                    _worldTravel.Reset();
                    _hasHuntingDestination = false;
                    _huntingDestinationEntry = 0;
                    _huntingDestinationSpawnId = 0;
                    _destinationRefreshMs = 1000;
                    ++_unreachableAnchorCount;
                    if (Helper::GrindFallbackPolicy::ShouldEndAfterUnreachableAnchor(_unreachableAnchorCount))
                    {
                        Finish(ActionOutcome::RetryableFailure,
                            "three distinct hunting anchors failed live/path preflight",
                            FailureCategory::ProgressionDifficulty,
                            RecoveryDirective::RetryLater);
                    }
                    return;
                }
                else if (travelResult == Travel::TravelResult::Arrived)
                {
                    _worldTravel.Reset();
                    _hasHuntingDestination = false;
                    _huntingDestinationEntry = 0;
                    _huntingDestinationSpawnId = 0;
                    _destinationRefreshMs = 0;
                    movement->Stop();
                }
                return;
            }
        }

        if (_hasHuntingDestination &&
            movement->GetState() != BotMovementState::Idle)
        {
            _huntingTravelProgress.Observe(
                { bot->GetPositionX(), bot->GetPositionY(),
                    bot->GetPositionZ() },
                { _huntingDestinationX, _huntingDestinationY,
                    bot->GetPositionZ() },
                deltaMs);
            uint32_t noProgressMs =
                _huntingTravelProgress.GetNoProgressMs();
            if (Helper::GrindFallbackPolicy::ShouldEndStalledHuntingTravel(
                noProgressMs))
            {
                uint32_t expirySec = Helper::MonotonicSeconds() +
                    Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
                if (_huntingDestinationSpawnId != 0)
                    _suppressedSpawnIds[_huntingDestinationSpawnId] = expirySec;
                else if (_huntingDestinationEntry != 0)
                    _suppressedCreatureEntries[_huntingDestinationEntry] = expirySec;
                _suppressedDestinations.push_back({ bot->GetMapId(),
                    _huntingDestinationX, _huntingDestinationY, 30.0f,
                    expirySec });
                if (_suppressedDestinations.size() >
                    Brain::SuppressionRegistry::MaxGrindDestinations)
                {
                    _suppressedDestinations.erase(
                        _suppressedDestinations.begin());
                }

                Diagnostics::StructuredEvent event;
                event.event = "grind_hunting_travel_stalled";
                event.goal = "Grind";
                event.action = GetName();
                event.requestX = _huntingDestinationX;
                event.requestY = _huntingDestinationY;
                event.requestZ = bot->GetPositionZ();
                event.pathFailure = movement->GetLastPathFailureName();
                event.pathFlags = movement->GetLastPathFlags();
                event.pathAttemptGeneration =
                    movement->GetPathAttemptGeneration();
                std::ostringstream details;
                details << "entry=" << _huntingDestinationEntry
                    << ";spawn=" << _huntingDestinationSpawnId
                    << ";no_progress_ms=" << noProgressMs
                    << ";suppression_s="
                    << Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));

                movement->Stop();
                _huntingTravelProgress.Reset();
                Finish(ActionOutcome::RetryableFailure,
                    "hunting-ground travel remained inside a repeated incomplete-path pocket",
                    FailureCategory::ProgressionDifficulty,
                    RecoveryDirective::RetryLater);
                return;
            }
        }

        if (movement->GetState() == BotMovementState::Idle)
        {
            float huntingDistance = _hasHuntingDestination
                ? Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                    _huntingDestinationX, _huntingDestinationY)
                : 0.0f;

            // A remote route is validated one executable leg at a time. If a
            // later continuation cannot produce its next leg, attribute that
            // evidence to the active anchor now rather than silently selecting
            // and retrying the same DB spawn after the refresh timer expires.
            if (_hasHuntingDestination &&
                huntingDistance > Constants::TacticalScanRadius &&
                movement->GetLastPathFailure() != BotPathFailure::None)
            {
                if ((movement->GetLastPathFlags() &
                    Helper::MovementPathPolicy::FarFromPolyStart) != 0)
                {
                    // The bot's origin is detached, so every destination would
                    // fail. Keep the anchor innocent and let shared origin
                    // recovery consume the path evidence.
                    _huntingDestinationEntry = 0;
                    _huntingDestinationSpawnId = 0;
                    _hasHuntingDestination = false;
                    _huntingTravelProgress.Reset();
                    _destinationRefreshMs = 1000;
                    return;
                }

                uint32_t expirySec = Helper::MonotonicSeconds() +
                    Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
                if (_huntingDestinationSpawnId != 0)
                    _suppressedSpawnIds[_huntingDestinationSpawnId] = expirySec;
                else if (_huntingDestinationEntry != 0)
                    _suppressedCreatureEntries[_huntingDestinationEntry] = expirySec;
                _suppressedDestinations.push_back({ bot->GetMapId(),
                    _huntingDestinationX, _huntingDestinationY, 30.0f,
                    expirySec });
                if (_suppressedDestinations.size() >
                    Brain::SuppressionRegistry::MaxGrindDestinations)
                {
                    _suppressedDestinations.erase(
                        _suppressedDestinations.begin());
                }

                ++_unreachableAnchorCount;
                Diagnostics::StructuredEvent event;
                event.event = "grind_anchor_continuation_failed";
                event.goal = "Grind";
                event.action = GetName();
                event.requestX = _huntingDestinationX;
                event.requestY = _huntingDestinationY;
                event.requestZ = bot->GetPositionZ();
                event.pathFailure = movement->GetLastPathFailureName();
                event.pathFlags = movement->GetLastPathFlags();
                event.pathAttemptGeneration =
                    movement->GetPathAttemptGeneration();
                std::ostringstream details;
                details << "entry=" << _huntingDestinationEntry
                    << ";spawn=" << _huntingDestinationSpawnId
                    << ";remaining_distance=" << huntingDistance
                    << ";unreachable_anchors=" << _unreachableAnchorCount
                    << ";suppression_s="
                    << Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds;
                event.details = details.str();
                Diagnostics::StructuredEventLog::Write(bot, std::move(event));

                _huntingDestinationEntry = 0;
                _huntingDestinationSpawnId = 0;
                _hasHuntingDestination = false;
                _huntingTravelProgress.Reset();
                _destinationRefreshMs = 0;
                _anchorSearchDiagnostics = "continuation_path_rejected";
                RefreshCandidateDiagnostics();
                if (Helper::GrindFallbackPolicy::ShouldEndAfterUnreachableAnchor(
                    _unreachableAnchorCount))
                {
                    Finish(ActionOutcome::RetryableFailure,
                        "three distinct hunting anchors failed bounded path validation",
                        FailureCategory::ProgressionDifficulty,
                        RecoveryDirective::RetryLater);
                    return;
                }
            }

            // Reaching a static anchor without observing a safe live target is
            // evidence that this exact location is stale or has an unsafe pack.
            // Remember it briefly so the expanding search actually moves on.
            if (_hasHuntingDestination &&
                Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                    _huntingDestinationX, _huntingDestinationY) <=
                    Constants::TacticalScanRadius)
            {
                if (_huntingDestinationSpawnId != 0)
                {
                    _suppressedSpawnIds[_huntingDestinationSpawnId] =
                        Helper::MonotonicSeconds() +
                        Helper::GrindFallbackPolicy::UnproductiveAnchorSuppressionSeconds;
                }
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' found no safe live target at hunting entry {}; skipping that anchor for {} seconds",
                        bot->GetName(), _huntingDestinationEntry,
                        Helper::GrindFallbackPolicy::UnproductiveAnchorSuppressionSeconds);
                _huntingDestinationEntry = 0;
                _huntingDestinationSpawnId = 0;
                _hasHuntingDestination = false;
                _unreachableAnchorCount = 0;
                _huntingTravelProgress.Reset();
                _destinationRefreshMs = 0;
                _anchorSearchDiagnostics = "reached_without_safe_live_target";
                RefreshCandidateDiagnostics();
            }

            if (_unproductiveMs >= Helper::GrindFallbackPolicy::UnproductiveTimeoutMs)
            {
                Finish(ActionOutcome::RetryableFailure,
                    _areaRelocationAttempted
                        ? "local expansion and viable-area relocation found no safe live grinding target"
                        : "local expansion found no safe live grinding target",
                    FailureCategory::ProgressionDifficulty,
                    RecoveryDirective::RetryLater);
                return;
            }

            if (_destinationRefreshMs == 0)
                TravelToHuntingGround(bot, movement);
        }
    }

    void GrindAction::RecoverFromNoDamageStall(Player* bot, Creature* target,
        MovementManager* movement)
    {
        if (!bot || !target || !bot->IsInWorld() || !target->IsInWorld() ||
            target->GetMap() != bot->GetMap())
            return;

        float targetX = target->GetPositionX();
        float targetY = target->GetPositionY();
        float targetZ = target->GetPositionZ();
        float facing = bot->GetAbsoluteAngle(target);
        ObjectGuid targetGuid = target->GetGUID();
        uint32 targetEntry = target->GetEntry();

        if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
        {
            TC_LOG_WARN("server", "[WorldBots] [Grind] Bot '{}' (GUID: {}) made no damage progress for {} seconds against NPC '{}' (Entry: {}, GUID: {}); relocating exactly to the NPC at ({:.2f}, {:.2f}, {:.2f})",
                bot->GetName(), bot->GetGUID().GetCounter(),
                Helper::CombatProgressWatchdog::StallTimeoutMs / 1000,
                target->GetName(), targetEntry, targetGuid.GetCounter(),
                targetX, targetY, targetZ);
        }

        movement->Stop();
        bot->NearTeleportTo(targetX, targetY, targetZ, facing);
        if (!Helper::TeleportUtils::CompletePendingTeleport(bot))
        {
            if (Diagnostics::BotTrace::ShouldLog(bot,
                Diagnostics::LogEvent::Normal))
            {
                TC_LOG_ERROR("server", "[WorldBots] [Grind] Bot '{}' could not complete its exact combat-stall relocation to NPC Entry {}",
                    bot->GetName(), targetEntry);
            }
            return;
        }

        Unit* refreshedTarget = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (refreshedTarget && refreshedTarget->IsAlive() &&
            refreshedTarget->IsInWorld())
        {
            bot->SetInFront(refreshedTarget);
            bot->Attack(refreshedTarget, true);
        }
    }

    void GrindAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot)
            bot->AttackStop();
        if (movement)
            movement->Stop();
        _targetGuid.Clear();
        _targetEntry = 0;
        _liveTargetPathFailures.Reset();
        _liveTargetApproachProgress.Reset();
        _combatProgressWatchdog.Reset();
        _huntingTravelProgress.Reset();
        _huntingDestinationEntry = 0;
        _huntingDestinationSpawnId = 0;
        _huntingDestinationZ = 0.0f;
        _hasHuntingDestination = false;
        _worldTravel.Reset();
    }
}
