#include "StuckDetector.h"
#include "Helper/TeleportUtils.h"
#include "Globals/ObjectMgr.h"
#include "Helper/MathUtils.h"
#include "DBCStructure.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include <ctime>

namespace Helper
{
    StuckDetector::StuckDetector()
        : _lastX(0.0f), _lastY(0.0f), _lastZ(0.0f),
          _stuckTimerMs(0), _sampleTimerMs(0), _stuckCount(0),
          _stuckZoneX(0.0f), _stuckZoneY(0.0f), _stuckZoneZ(0.0f),
          _stuckZoneTimestampSec(0), _sameZoneStuckCount(0)
    {
    }

    void StuckDetector::Reset()
    {
        _stuckTimerMs = 0;
        _sampleTimerMs = 0;
        _stuckCount = 0;
    }

    bool StuckDetector::Update(Player* bot, MovementManager* movement, Brain::BotGoal goal,
                               Blackboard::BotBlackboard& blackboard,
                               uint32_t activeQuestId,
                               std::unordered_map<uint32_t, uint32_t>& blacklistedQuests,
                               std::unordered_map<uint32_t, uint32_t>& blacklistedNpcs,
                               uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive()) return false;

        // Stuck detection is ONLY active during active movement towards a destination
        // Disabled during Combat, Resurrect, Rest/Sitting, Casting, or when MovementManager is Idle
        bool isActivelyMoving = movement && movement->HasPath() &&
                               (movement->GetState() == BotMovementState::Moving ||
                                movement->GetState() == BotMovementState::Chasing ||
                                movement->GetState() == BotMovementState::Fleeing);
        bool isStationaryState = bot->IsSitState() || bot->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_CHARGING) || goal == Brain::BotGoal::Rest || goal == Brain::BotGoal::Idle;

        if (goal != Brain::BotGoal::Combat && goal != Brain::BotGoal::Resurrect && !bot->IsInCombat() && isActivelyMoving && !isStationaryState)
        {
            _sampleTimerMs += deltaMs;
            if (_sampleTimerMs >= 1000)
            {
                _sampleTimerMs -= 1000;
                float curX = bot->GetPositionX();
                float curY = bot->GetPositionY();
                float curZ = bot->GetPositionZ();

                float distSq = Helper::DistanceSq(curX, curY, curZ, _lastX, _lastY, _lastZ);

                // If already stuck (4+ seconds), require moving >= 2.0 yards (distSq >= 4.0f) to consider unstuck and reset timer
                // If not yet stuck (< 4 seconds), require moving >= 0.2 yards (distSq >= 0.04f) to show progress
                float requiredDistSq = (_stuckTimerMs >= 4000) ? 4.0f : 0.04f;

                if (distSq >= requiredDistSq) // Bot is making significant progress!
                {
                    _lastX = curX;
                    _lastY = curY;
                    _lastZ = curZ;
                    _stuckTimerMs = 0;
                    _stuckCount = 0;
                }
                else
                {
                    _stuckTimerMs += 1000;

                    if (_stuckTimerMs >= 4000) // Stationary/Stuck for 4+ seconds in an active movement goal!
                    {
                        _stuckCount++;
                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' GLOBAL STUCK DETECTED at ({:.1f}, {:.1f}, {:.1f}) in Goal! (Stuck for {} seconds, Attempt {})",
                                bot->GetName(), curX, curY, curZ, _stuckTimerMs / 1000, _stuckCount);

                        uint32_t nowSec = static_cast<uint32_t>(time(nullptr));

                        // Same-Zone repeated stuck tracking
                        if (_stuckTimerMs == 4000)
                        {
                            float zoneDistSq = Helper::DistanceSq(curX, curY, curZ, _stuckZoneX, _stuckZoneY, _stuckZoneZ);
                            if (nowSec - _stuckZoneTimestampSec <= 60 && zoneDistSq <= 100.0f) // Within 10 yards in last 60s
                            {
                                _sameZoneStuckCount++;
                            }
                            else
                            {
                                _stuckZoneX = curX;
                                _stuckZoneY = curY;
                                _stuckZoneZ = curZ;
                                _stuckZoneTimestampSec = nowSec;
                                _sameZoneStuckCount = 1;
                            }

                            if (_sameZoneStuckCount >= 3)
                            {
                                uint32_t targetKey = 0;
                                if (goal == Brain::BotGoal::AcceptQuest || goal == Brain::BotGoal::TurnInQuest)
                                    targetKey = activeQuestId;
                                else if (goal == Brain::BotGoal::Vendor || goal == Brain::BotGoal::TownRun)
                                    targetKey = blackboard.inv.nearestVendorEntry;
                                else if (goal == Brain::BotGoal::ProgressQuest)
                                    targetKey = activeQuestId;

                                if (targetKey != 0)
                                {
                                    uint32_t expirySec = nowSec + 300; // 5-minute blacklist
                                    if (goal == Brain::BotGoal::Vendor || goal == Brain::BotGoal::TownRun)
                                        blacklistedNpcs[targetKey] = expirySec;
                                    else
                                        blacklistedQuests[targetKey] = expirySec;
                                    TC_LOG_WARN("server", "[WorldBots] [Brain] Bot '{}' STUCK IN SAME ZONE 3+ TIMES! Target ID {} blacklisted for 5 minutes.",
                                        bot->GetName(), targetKey);
                                }

                                if (movement) movement->Stop();
                                Reset();
                                return true;
                            }
                        }

                        // 1. Jump animation hop
                        bot->HandleEmoteCommand(static_cast<Emote>(39));

                        // 2. Calculate evasion point (random angle & distance between 4 and 8 yards)
                        G3D::Vector3 evadPoint = EvasionUtils::CalculateRandomEvasionPoint(bot, 4.0f, 8.0f);
                        float evadX = evadPoint.x, evadY = evadPoint.y, evadZ = evadPoint.z;
                        float o = bot->GetOrientation();

                        if (_stuckTimerMs >= 12000) // 12 seconds severe deadlock: Teleport to nearest Spirit Healer / Graveyard
                        {
                            if ((goal == Brain::BotGoal::Vendor || goal == Brain::BotGoal::TownRun) &&
                                blackboard.inv.nearestVendorEntry != 0)
                            {
                                uint32_t expirySec = static_cast<uint32_t>(time(nullptr)) + 300; // 5-minute blacklist
                                blacklistedNpcs[blackboard.inv.nearestVendorEntry] = expirySec;
                                TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' Target Vendor NPC (Entry: {}) UNREACHABLE! Blacklisting vendor for 5 minutes.",
                                    bot->GetName(), blackboard.inv.nearestVendorEntry);
                            }
                            else if (goal == Brain::BotGoal::ProgressQuest && activeQuestId != 0)
                            {
                                uint32_t expirySec = static_cast<uint32_t>(time(nullptr)) + 900; // 15-minute blacklist
                                blacklistedQuests[activeQuestId] = expirySec;
                                TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' Quest {} UNREACHABLE / DEADLOCKED! Blacklisting quest for 15 minutes.",
                                    bot->GetName(), activeQuestId);
                            }

                            if (Diagnostics::BotTrace::ShouldLog(bot))
                                TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' SEVERE DEADLOCK! Teleporting to nearest Spirit Healer / Graveyard!",
                                    bot->GetName());

                            if (WorldSafeLocsEntry const* graveyard = sObjectMgr->GetClosestGraveyard(curX, curY, curZ, bot->GetMapId(), bot->GetTeamId()))
                            {
                                bot->TeleportTo(graveyard->Continent, graveyard->Loc.X, graveyard->Loc.Y, graveyard->Loc.Z, bot->GetOrientation());
                            }
                            else
                            {
                                bot->NearTeleportTo(evadX, evadY, evadZ, o);
                            }
                            TeleportUtils::CompletePendingTeleport(bot);

                            if (movement) movement->Stop();
                            Reset();
                        }
                        else if (_stuckTimerMs >= 10000 && (goal == Brain::BotGoal::AcceptQuest || goal == Brain::BotGoal::TurnInQuest ||
                            goal == Brain::BotGoal::Vendor || goal == Brain::BotGoal::TownRun))
                        {
                            uint32_t targetKey = 0;
                            if (goal == Brain::BotGoal::AcceptQuest || goal == Brain::BotGoal::TurnInQuest)
                                targetKey = activeQuestId;
                            else if (goal == Brain::BotGoal::Vendor || goal == Brain::BotGoal::TownRun)
                                targetKey = blackboard.inv.nearestVendorEntry;

                            if (targetKey != 0)
                            {
                                uint32_t expirySec = static_cast<uint32_t>(time(nullptr)) + 300; // 5-minute blacklist
                                if (goal == Brain::BotGoal::Vendor || goal == Brain::BotGoal::TownRun)
                                    blacklistedNpcs[targetKey] = expirySec;
                                else
                                    blacklistedQuests[targetKey] = expirySec;
                                TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' Target NPC/Quest ID {} UNREACHABLE after 10s! Blacklisting for 5 minutes and skipping.",
                                    bot->GetName(), targetKey);
                            }

                            if (movement) movement->Stop();
                            Reset();
                        }
                        else if (_stuckTimerMs >= 8000) // 8 seconds: try a wider navmesh recovery without resetting escalation
                        {
                            G3D::Vector3 wideEvad = EvasionUtils::CalculateRandomEvasionPoint(bot, 6.0f, 10.0f);
                            if (movement) movement->MoveTo(wideEvad.x, wideEvad.y, wideEvad.z, BotMovementState::Moving, true);
                        }
                        else
                        {
                            // 4 seconds: issue sidestep evasion move
                            if (movement) movement->MoveTo(evadX, evadY, evadZ);
                        }

                        _lastX = curX;
                        _lastY = curY;
                        _lastZ = curZ;
                        return true;
                    }
                }
            }
        }
        else
        {
            _stuckTimerMs = 0;
            _sampleTimerMs = 0;
            _lastX = bot->GetPositionX();
            _lastY = bot->GetPositionY();
            _lastZ = bot->GetPositionZ();
        }
        return false;
    }
}
