#include "UnstuckAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Cache/BotCache.h"
#include "Config/BotConfig.h"
#include "Helper/RecoveryHubPolicy.h"
#include "Helper/TeleportUtils.h"
#include "Helper/GrindFallbackPolicy.h"
#include "Helper/MathUtils.h"
#include "Helper/MovementPathPolicy.h"
#include "PathGenerator.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Log.h"

#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace
{
    std::unordered_map<uint64, std::vector<Cache::PositionInfo>> s_rejectedRecoveryHubs;
    std::unordered_map<uint64, Cache::PositionInfo> s_lastRecoveryHub;

    bool HasReachableProgressionEcology(Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return false;

        constexpr float MinimumAnchorDistance = 5.0f;
        constexpr float MaximumEcologyDistance = 1800.0f;
        constexpr uint32 MaximumAnchorPreflights = 8;
        std::unordered_set<uint64_t> excludedSpawns;
        std::unordered_set<uint32_t> excludedEntries;
        for (uint32 attempt = 0; attempt < MaximumAnchorPreflights; ++attempt)
        {
            Cache::PositionInfo anchor;
            uint32_t creatureEntry = 0;
            uint64_t spawnId = 0;
            if (!Cache::BotCache::FindNearestGrindingCreature(bot,
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                bot->GetMapId(), MinimumAnchorDistance,
                Config::BotConfig::GetGrindMinLevelOffset(),
                Config::BotConfig::GetGrindMaxLevelOffset(), anchor,
                creatureEntry, excludedSpawns, excludedEntries, {},
                MaximumEcologyDistance, &spawnId))
            {
                break;
            }

            Cache::PositionInfo liveAnchor;
            uint32_t liveCreatureEntry = 0;
            uint64_t liveSpawnId = 0;
            constexpr float LiveAnchorRadius = 300.0f;
            if (!Cache::BotCache::ResolveLiveGrindingAnchor(bot, anchor,
                LiveAnchorRadius,
                Config::BotConfig::GetGrindMinLevelOffset(),
                Config::BotConfig::GetGrindMaxLevelOffset(), liveAnchor,
                liveCreatureEntry, liveSpawnId, excludedSpawns,
                excludedEntries))
            {
                if (spawnId != 0)
                    excludedSpawns.insert(spawnId);
                else if (creatureEntry != 0)
                    excludedEntries.insert(creatureEntry);
                continue;
            }

            float anchorDist = Helper::Distance2D(bot->GetPositionX(),
                bot->GetPositionY(), liveAnchor.x, liveAnchor.y);
            if (anchorDist <= Helper::GrindFallbackPolicy::CompletePathPreflightRadius)
            {
                if (Helper::TeleportUtils::HasCompleteGroundPathTo(bot,
                    liveAnchor.x, liveAnchor.y, liveAnchor.z))
                {
                    return true;
                }
            }
            else
            {
                // Beyond Trinity's 180-yard smooth path budget, verify that the bot has
                // a valid ground origin and that a straight corridor route exists to the destination.
                if (Helper::TeleportUtils::HasUsableGroundOrigin(bot))
                {
                    PathGenerator route(bot);
                    route.SetUseStraightPath(true);
                    if (route.CalculatePath(liveAnchor.x, liveAnchor.y, liveAnchor.z))
                    {
                        uint32 flags = static_cast<uint32>(route.GetPathType());
                        if ((flags & Helper::MovementPathPolicy::FarFromPolyStart) == 0 &&
                            (flags & Helper::MovementPathPolicy::FarFromPolyEnd) == 0)
                        {
                            return true;
                        }
                    }
                }
            }

            if (liveSpawnId != 0)
                excludedSpawns.insert(liveSpawnId);
            else if (liveCreatureEntry != 0)
                excludedEntries.insert(liveCreatureEntry);
        }

        return false;
    }
}

namespace Actions
{
    UnstuckAction::UnstuckAction(uint32_t deadlyQuestId,
        bool progressionRecovery)
        : _deadlyQuestId(deadlyQuestId),
          _progressionRecovery(progressionRecovery)
    {
    }

    void UnstuckAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        _materialEcologyChange = false;
        if (!bot || !bot->IsInWorld())
        {
            Finish(ActionOutcome::RetryableFailure, "unstuck context was unavailable",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        ObjectGuid guid = bot->GetGUID();
        uint32 originMapId = bot->GetMapId();
        uint32 originZoneId = bot->GetZoneId();
        float originX = bot->GetPositionX();
        float originY = bot->GetPositionY();
        float originZ = bot->GetPositionZ();
        float originOrientation = bot->GetOrientation();
        bool originPathRecovery = movement && movement->NeedsOriginPathRecovery();

        TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) BRUTE-FORCE UNSTUCK: Relocating for {}, saving DB, and preserving the active session...",
            bot->GetName(), guid.GetCounter(),
            _progressionRecovery ? "reachable progression ecology" : "safe travel recovery");

        if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
        {
            Diagnostics::StructuredEvent event;
            event.event = "unstuck_started";
            event.goal = "Recovery";
            event.action = "UnstuckAction";
            event.requestX = originX;
            event.requestY = originY;
            event.requestZ = bot->GetPositionZ();
            event.details = _progressionRecovery ? "type=progression_recovery" : "type=navigation_recovery";
            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
        }
        Diagnostics::BotTrace::LogToFile(bot, "Recovery",
            std::string("UnstuckAction started at (") + std::to_string(originX) + ", " +
            std::to_string(originY) + ", " + std::to_string(bot->GetPositionZ()) + ") type=" +
            (_progressionRecovery ? "progression" : "navigation"), Diagnostics::LogEvent::Important);

        // 1. Abandon deadly quest in TrinityCore QuestLog to prevent re-populating in Blackboard
        if (_deadlyQuestId != 0 && bot->GetQuestStatus(_deadlyQuestId) != QUEST_STATUS_NONE)
        {
            bot->AbandonQuest(_deadlyQuestId);
            TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' ABANDONED deadly quest {}!",
                bot->GetName(), _deadlyQuestId);
        }

        // 2. Stop combat and clear threat
        bot->CombatStop(true);
        bot->ClearInCombat();

        // 3. This path is only reached after repeated deaths. Restore a viable
        // combat state as well as position; otherwise broken equipment sends
        // the bot straight back into the same death loop after teleporting.
        bot->ResurrectPlayer(1.0f, false);
        bot->SpawnCorpseBones();
        bot->DurabilityRepairAll(false, 1.0f, false);
        bot->SetHealth(bot->GetMaxHealth());

        // Stop while the player is still resolvable. Calling Stop after
        // TeleportTo leaves the old destination alive until the next runtime
        // pass because MovementManager rejects teleporting players.
        if (movement)
            movement->Stop();

        // 4. Preserve local progress when the bot is merely on the wrong
        // vertical navmesh layer. The candidate comes from Trinity's nearest
        // polygon and must prove that it can start ground paths before it is
        // accepted. A safe travel hub remains the fallback.
        bool usedLocalNavmeshRecovery = !_progressionRecovery &&
            originPathRecovery &&
            Helper::TeleportUtils::TryRelocateToLocalNavmesh(bot);

        // 5. Prefer a same-map friendly recovery hub. Progression recovery
        // additionally requires a material ecology change and a complete path
        // to a level-appropriate grinding anchor. Homebind is only successful
        // when it satisfies those same progression checks.
        uint64 botKey = guid.GetRawValue();
        std::vector<Cache::PositionInfo>& rejected = s_rejectedRecoveryHubs[botKey];
        auto lastHub = s_lastRecoveryHub.find(botKey);
        if (lastHub != s_lastRecoveryHub.end())
        {
            rejected.push_back(lastHub->second);
            s_lastRecoveryHub.erase(lastHub);
        }

        Cache::PositionInfo recoveryPosition;
        uint32_t flightMasterEntry = 0;
        bool usedRecoveryHub = false;
        constexpr uint32 MaxRecoveryHubAttempts = 4;

        auto tryLocateRecoveryHubOnMap = [&](uint32 searchMapId, float searchX, float searchY) -> bool {
            for (uint32 attempt = 0; attempt < MaxRecoveryHubAttempts; ++attempt)
            {
                if (!Cache::BotCache::FindNearestSafeRecoveryHub(
                    bot, searchMapId, searchX, searchY,
                    recoveryPosition, flightMasterEntry, rejected,
                    _progressionRecovery))
                {
                    break;
                }

                constexpr float RecoveryHeightOffset = 0.5f;
                bot->TeleportTo(recoveryPosition.mapId, recoveryPosition.x,
                    recoveryPosition.y, recoveryPosition.z + RecoveryHeightOffset,
                    bot->GetOrientation());
                bool transferComplete =
                    Helper::TeleportUtils::CompletePendingTeleport(bot);
                bool usableOrigin = transferComplete &&
                    Helper::TeleportUtils::HasUsableGroundOrigin(bot);
                bool materialEcologyChange = usableOrigin &&
                    Helper::RecoveryHubPolicy::IsMaterialEcologyChange(
                        originMapId, originZoneId, originX, originY,
                        bot->GetMapId(), bot->GetZoneId(), bot->GetPositionX(),
                        bot->GetPositionY());
                bool reachableEcology = !_progressionRecovery ||
                    (materialEcologyChange &&
                        HasReachableProgressionEcology(bot));
                if (usableOrigin && reachableEcology)
                {
                    _materialEcologyChange = materialEcologyChange;
                    s_lastRecoveryHub[botKey] = recoveryPosition;
                    bool shouldUpdateBind = (Helper::TeleportUtils::IsHomebindInStarterArea(bot) && bot->GetLevel() >= 6) ||
                        (Helper::TeleportUtils::IsHomebindInStarterZone(bot) && bot->GetLevel() >= 10) ||
                        (bot->GetLevel() >= 10);
                    if (shouldUpdateBind)
                    {
                        uint32 areaId = bot->GetAreaId();
                        if (Map* map = bot->GetMap())
                        {
                            uint32 mapArea = map->GetAreaId(bot->GetPhaseMask(), recoveryPosition.x, recoveryPosition.y, recoveryPosition.z);
                            if (mapArea != 0)
                                areaId = mapArea;
                        }
                        Helper::TeleportUtils::SetHomebind(bot, recoveryPosition.mapId,
                            areaId, recoveryPosition.x, recoveryPosition.y, recoveryPosition.z);
                    }
                    if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
                    {
                        Diagnostics::StructuredEvent event;
                        event.event = "unstuck_hub_relocated";
                        event.goal = "Recovery";
                        event.action = "UnstuckAction";
                        event.requestX = recoveryPosition.x;
                        event.requestY = recoveryPosition.y;
                        event.requestZ = recoveryPosition.z;
                        event.outcome = "Succeeded";
                        event.details = "entry=" + std::to_string(flightMasterEntry) +
                            ";map=" + std::to_string(recoveryPosition.mapId);
                        Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                    }
                    Diagnostics::BotTrace::LogToFile(bot, "Recovery",
                        "Relocated to recovery hub entry=" + std::to_string(flightMasterEntry) +
                        " on map " + std::to_string(recoveryPosition.mapId), Diagnostics::LogEvent::Important);

                    if (_progressionRecovery)
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Recovery] Bot '{}' relocated to materially different level-appropriate ecology at validated hub Entry {} ({:.1f}, {:.1f}, {:.1f}) on Map {}",
                            bot->GetName(), flightMasterEntry, recoveryPosition.x,
                            recoveryPosition.y, recoveryPosition.z,
                            recoveryPosition.mapId);
                    }
                    else
                    {
                        TC_LOG_INFO("server", "[WorldBots] [Recovery] Bot '{}' relocated to validated level-safe travel hub Entry {} at ({:.1f}, {:.1f}, {:.1f}) on Map {}",
                            bot->GetName(), flightMasterEntry, recoveryPosition.x,
                            recoveryPosition.y, recoveryPosition.z,
                            recoveryPosition.mapId);
                    }
                    return true;
                }

                rejected.push_back(recoveryPosition);
                if (_progressionRecovery)
                {
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' rejected recovery hub Entry {} because it did not provide a materially different reachable level-appropriate ecology; trying an alternate hub",
                        bot->GetName(), flightMasterEntry);
                }
                else
                {
                    TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' rejected recovery hub Entry {} because its origin could not start a safe ground path; trying an alternate hub",
                        bot->GetName(), flightMasterEntry);
                }
            }
            bot->TeleportTo(originMapId, originX, originY, originZ, originOrientation);
            Helper::TeleportUtils::CompletePendingTeleport(bot);
            return false;
        };

        if (!usedLocalNavmeshRecovery)
        {
            usedRecoveryHub = tryLocateRecoveryHubOnMap(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());

            // If a Level 12+ bot is stranded in a starter zone during progression recovery,
            // immediately target canonical secondary progression hubs on their home continent.
            if (!usedRecoveryHub && _progressionRecovery && bot->GetLevel() >= 12 &&
                Helper::RecoveryHubPolicy::IsStarterZone(originMapId, originZoneId))
            {
                if (bot->GetTeamId() == TEAM_HORDE)
                {
                    if (originMapId == 1) // Kalimdor Horde -> The Crossroads
                        usedRecoveryHub = tryLocateRecoveryHubOnMap(1, -457.7f, -2639.8f);
                    else if (originMapId == 0) // Eastern Kingdoms Horde -> The Sepulcher
                        usedRecoveryHub = tryLocateRecoveryHubOnMap(0, 518.7f, 1606.5f);
                }
                else // TEAM_ALLIANCE
                {
                    if (originMapId == 0) // Eastern Kingdoms Alliance -> Sentinel Hill or Thelsamar
                    {
                        usedRecoveryHub = tryLocateRecoveryHubOnMap(0, -10636.5f, 1036.9f);
                        if (!usedRecoveryHub)
                            usedRecoveryHub = tryLocateRecoveryHubOnMap(0, -5384.8f, -2954.2f);
                    }
                    else if (originMapId == 1) // Kalimdor Alliance -> Auberdine
                    {
                        usedRecoveryHub = tryLocateRecoveryHubOnMap(1, 6433.0f, 513.7f);
                    }
                }
            }

            if (!usedRecoveryHub && (bot->GetLevel() >= 10 || originMapId == 530))
            {
                uint32 factionPrimaryMap = (bot->GetTeamId() == TEAM_ALLIANCE ? 0 : 1);
                if (factionPrimaryMap != originMapId)
                    usedRecoveryHub = tryLocateRecoveryHubOnMap(factionPrimaryMap, 0.0f, 0.0f);
            }
            if (!usedRecoveryHub && bot->GetLevel() >= 15)
            {
                uint32 factionSecondaryMap = (bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 0);
                if (factionSecondaryMap != originMapId)
                    usedRecoveryHub = tryLocateRecoveryHubOnMap(factionSecondaryMap, 0.0f, 0.0f);
            }
        }

        bool usedBindPosition = false;
        if (!usedLocalNavmeshRecovery && !usedRecoveryHub)
        {
            bool homebindInStarterArea = Helper::TeleportUtils::IsHomebindInStarterArea(bot);
            bool homebindInStarterZone = Helper::TeleportUtils::IsHomebindInStarterZone(bot);
            if ((homebindInStarterArea && bot->GetLevel() >= 6) ||
                (homebindInStarterZone && bot->GetLevel() >= 10))
            {
                TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' rejected homebind in starter area/zone; deferring progression recovery",
                    bot->GetName());
                if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
                {
                    Diagnostics::StructuredEvent event;
                    event.event = "unstuck_homebind_rejected";
                    event.goal = "Recovery";
                    event.action = "UnstuckAction";
                    event.requestX = bot->m_homebindX;
                    event.requestY = bot->m_homebindY;
                    event.requestZ = bot->m_homebindZ;
                    event.outcome = "Blocked";
                    event.details = "starter_zone_regression_prevented";
                    Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                }
                Diagnostics::BotTrace::LogToFile(bot, "Recovery",
                    "Rejected homebind in starter zone/area to prevent regression", Diagnostics::LogEvent::Important);

                bot->SaveToDB();
                Finish(ActionOutcome::RetryableFailure,
                    "progression recovery avoided starter-zone homebind regression",
                    FailureCategory::ProgressionDifficulty,
                    RecoveryDirective::RetryLater);
                return;
            }

            Helper::TeleportUtils::TeleportToHomebind(bot);
            if (_progressionRecovery)
            {
                bool usableOrigin = Helper::TeleportUtils::HasUsableGroundOrigin(bot);
                _materialEcologyChange = usableOrigin &&
                    Helper::RecoveryHubPolicy::IsMaterialEcologyChange(
                        originMapId, originZoneId, originX, originY,
                        bot->GetMapId(), bot->GetZoneId(),
                        bot->GetPositionX(), bot->GetPositionY());
                usedBindPosition = _materialEcologyChange &&
                    HasReachableProgressionEcology(bot);
                if (!usedBindPosition)
                {
                    bot->SaveToDB();
                    Finish(ActionOutcome::RetryableFailure,
                        _materialEcologyChange
                            ? "no reachable level-appropriate grinding ecology was validated after relocation"
                            : "progression recovery returned to the same bind ecology",
                        FailureCategory::ProgressionDifficulty,
                        RecoveryDirective::RetryLater);
                    return;
                }
                TC_LOG_INFO("server", "[WorldBots] [Recovery] Bot '{}' used a materially different bind position with validated reachable level-appropriate ecology",
                    bot->GetName());
            }
            else
            {
                usedBindPosition = true;
                TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' found no validated alternate recovery hub; using its bind position",
                    bot->GetName());
            }
        }


        // 6. Save state to character DB
        bot->SaveToDB();

        // Keep the existing session alive. Lifecycle ownership belongs to the
        // central runtime; actions must not create competing login pipelines.
        Finish(ActionOutcome::Succeeded,
            _progressionRecovery
                ? "relocated to a materially different reachable level-appropriate grinding ecology"
                : (usedLocalNavmeshRecovery
                ? "corrected to the nearest validated local navmesh layer"
                : (usedRecoveryHub
                    ? "relocated to a level-safe friendly travel hub"
                    : (usedBindPosition
                        ? "relocated to a safe bind position"
                        : "relocation completed"))));
    }

    void UnstuckAction::Update(Player* /*bot*/, MovementManager* /*movement*/, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/)
    {
        if (!_completed)
        {
            Finish(ActionOutcome::RetryableFailure, "unstuck ended before relocation was attempted",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
        }
    }

    void UnstuckAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot && bot->IsNonMeleeSpellCast(false))
            bot->InterruptNonMeleeSpells(true);
        if (movement)
            movement->Stop();
    }

    void UnstuckAction::RecordProgress(ObjectGuid botGuid)
    {
        ResetRecoveryCandidates(botGuid);
    }

    void UnstuckAction::ResetRecoveryCandidates(ObjectGuid botGuid)
    {
        if (!botGuid)
            return;
        uint64 key = botGuid.GetRawValue();
        s_rejectedRecoveryHubs.erase(key);
        s_lastRecoveryHub.erase(key);
    }

    void UnstuckAction::ClearBotState(ObjectGuid botGuid)
    {
        ResetRecoveryCandidates(botGuid);
    }

    void UnstuckAction::ClearAllState()
    {
        s_rejectedRecoveryHubs.clear();
        s_lastRecoveryHub.clear();
    }
}
