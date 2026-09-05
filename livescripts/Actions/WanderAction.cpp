#include "WanderAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Cache/BotCache.h"
#include "Map.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Helper/DestinationSuppressionPolicy.h"
#include "Helper/TimeUtils.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <unordered_set>

namespace Actions
{
    WanderAction::WanderAction(float /*originX*/, float /*originY*/, float /*originZ*/, float /*radius*/,
        std::unordered_map<uint32_t, uint32_t> suppressedQuests,
        std::vector<Brain::DestinationSuppression> suppressedDestinations)
        : _pauseTimer(0), _suppressedQuests(std::move(suppressedQuests)),
          _suppressedDestinations(std::move(suppressedDestinations))
    {
    }

    void WanderAction::Start(Player* /*bot*/, MovementManager* movement)
    {
        ResetOutcome();
        if (!movement) return;
        _pauseTimer = 0;
        uint32_t nowSec = Helper::MonotonicSeconds();
        _suppressedDestinations.erase(std::remove_if(
            _suppressedDestinations.begin(), _suppressedDestinations.end(),
            [nowSec](const Brain::DestinationSuppression& destination) {
                return destination.untilSec <= nowSec;
            }), _suppressedDestinations.end());
        _destinationPathFailures.Reset();
        _recoveryPolicy.Reset();
        _requiresOriginRecovery = false;
    }

    uint64_t WanderAction::MakeDestinationKey(DestinationKind kind,
        uint32_t subjectId, float x, float y, float z)
    {
        uint64_t hash = Helper::HashUtils::FNV1aBasis;
        Helper::HashUtils::MixHash(hash, static_cast<uint8_t>(kind));
        Helper::HashUtils::MixHash(hash, subjectId);
        Helper::HashUtils::MixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(std::llround(x * 2.0f))));
        Helper::HashUtils::MixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(std::llround(y * 2.0f))));
        Helper::HashUtils::MixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(std::llround(z * 2.0f))));
        return hash;
    }

    bool WanderAction::IsDestinationSuppressed(uint32_t mapId,
        float x, float y) const
    {
        uint32_t nowSec = Helper::MonotonicSeconds();
        return std::any_of(_suppressedDestinations.begin(),
            _suppressedDestinations.end(),
            [=](const Brain::DestinationSuppression& destination) {
                return destination.Contains(mapId, x, y, nowSec);
            });
    }

    bool WanderAction::TryWanderMove(Player* bot, MovementManager* movement,
        DestinationKind kind, uint32_t subjectId, uint32_t questId,
        float x, float y, float z, const char* pathSource)
    {
        if (!bot || !movement || IsDestinationSuppressed(bot->GetMapId(), x, y))
            return false;

        std::ostringstream source;
        source << pathSource << ";subject=" << subjectId;
        if (questId != 0)
            source << ";quest=" << questId;
        movement->SetDiagnosticPathSource(source.str());

        uint64_t pathGeneration = movement->GetPathAttemptGeneration();
        bool started = movement->MoveTo(x, y, z,
            BotMovementState::Moving, false);
        if (started)
            _recoveryPolicy.Reset();
        bool freshAttempt = movement->GetPathAttemptGeneration() !=
            pathGeneration;
        uint64_t destinationKey = MakeDestinationKey(kind, subjectId, x, y, z);
        bool rejectDestination = _destinationPathFailures.Observe(
            destinationKey, freshAttempt,
            !started && movement->GetLastPathFailure() != BotPathFailure::None);
        if (!rejectDestination)
            return true;

        bool authoredDestination = kind != DestinationKind::Exploration;
        uint32_t destinationSuppressionSeconds =
            Helper::DestinationSuppressionPolicy::GetDurationSeconds(
                authoredDestination);
        uint32_t nowSec = Helper::MonotonicSeconds();
        uint32_t untilSec = nowSec + destinationSuppressionSeconds;
        _suppressedDestinations.erase(std::remove_if(
            _suppressedDestinations.begin(), _suppressedDestinations.end(),
            [nowSec](const Brain::DestinationSuppression& destination) {
                return destination.untilSec <= nowSec;
            }), _suppressedDestinations.end());
        _suppressedDestinations.push_back({
            bot->GetMapId(), x, y,
            Helper::DestinationSuppressionPolicy::GetRadius(
                authoredDestination), untilSec
        });
        constexpr std::size_t MaximumSuppressedDestinations =
            Brain::SuppressionRegistry::MaxWanderDestinations;
        if (_suppressedDestinations.size() > MaximumSuppressedDestinations)
            _suppressedDestinations.erase(_suppressedDestinations.begin());
        if (questId != 0)
            _suppressedQuests[questId] = untilSec;
        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' abandoned {} destination {} at ({:.1f}, {:.1f}, {:.1f}) for {} seconds after {} fresh path failures: {} (flags {})",
                bot->GetName(), pathSource, subjectId, x, y, z,
                destinationSuppressionSeconds,
                _destinationPathFailures.GetFailureCount(),
                movement->GetLastPathFailureName(),
                movement->GetLastPathFlags());
        }
        _destinationPathFailures.Reset();
        return false;
    }

    void WanderAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !movement || !bot->IsInWorld()) return;

        if (movement->GetState() == BotMovementState::Idle)
        {
            _pauseTimer += deltaMs;
            if (_pauseTimer >= Constants::WanderPauseIntervalMs)
            {
                _pauseTimer = 0;

                float curX = bot->GetPositionX();
                float curY = bot->GetPositionY();
                float curZ = bot->GetPositionZ();
                uint32_t mapId = bot->GetMapId();

                Cache::PositionInfo targetPos;
                uint32_t targetQuestId = 0;
                uint32_t targetNpcEntry = 0;
                bool targetIsGameObject = false;
                std::unordered_set<uint32_t> excludedQuestIds;
                uint32_t nowSec = Helper::MonotonicSeconds();
                for (const auto& [questId, expirySec] : _suppressedQuests)
                {
                    if (nowSec < expirySec)
                        excludedQuestIds.insert(questId);
                }

                // Priority 1: Search for an Available Quest Starter on this map
                if (Cache::BotCache::FindNearestAvailableQuestStarter(bot, curX, curY, curZ, mapId, targetPos,
                    targetQuestId, targetNpcEntry, targetIsGameObject, excludedQuestIds))
                {
                    float dist = Helper::Distance2D(curX, curY, targetPos.x, targetPos.y);
                    if (dist > 15.0f)
                    {
                        float targetZ = targetPos.z;

                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' wandering productively towards Quest Starter {} Entry {} (Quest {}) at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                                bot->GetName(), targetIsGameObject ? "GameObject" : "NPC", targetNpcEntry, targetQuestId, targetPos.x, targetPos.y, targetZ, dist);

                        if (TryWanderMove(bot, movement,
                            DestinationKind::QuestStarter, targetNpcEntry,
                            targetQuestId, targetPos.x, targetPos.y, targetZ,
                            "wander_quest_starter"))
                        {
                            return;
                        }
                    }

                    // Give the one-second blackboard scan time to discover the
                    // nearby starter. Do not immediately replace this goal with
                    // a settlement destination at the end of a route segment.
                    if (!IsDestinationSuppressed(mapId, targetPos.x,
                        targetPos.y))
                    {
                        return;
                    }
                }

                // Priority 2: Search for a Settlement / Vendor Hub on this map
                uint32_t targetVendorEntry = 0;
                if (Cache::BotCache::FindNearestSettlementOrVendor(mapId, curX, curY, curZ, 40.0f, targetPos, targetVendorEntry))
                {
                    float dist = Helper::Distance2D(curX, curY, targetPos.x, targetPos.y);
                    if (dist > 25.0f)
                    {
                        float targetZ = targetPos.z;

                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' wandering productively towards Settlement/Vendor Entry {} at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                                bot->GetName(), targetVendorEntry, targetPos.x, targetPos.y, targetZ, dist);

                        if (TryWanderMove(bot, movement,
                            DestinationKind::Settlement, targetVendorEntry, 0,
                            targetPos.x, targetPos.y, targetZ,
                            "wander_settlement"))
                        {
                            return;
                        }
                    }
                }

                // Priority 3: Search for Level-Appropriate Mobs on this map for grinding/exploration
                uint32_t targetCreatureEntry = 0;
                if (Cache::BotCache::FindNearestLevelAppropriateCreature(bot, curX, curY, curZ, mapId, 30.0f, targetPos, targetCreatureEntry))
                {
                    float dist = Helper::Distance2D(curX, curY, targetPos.x, targetPos.y);
                    if (dist > 20.0f)
                    {
                        float targetZ = targetPos.z;

                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' wandering productively towards Level-Appropriate Mobs (Entry {}) at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                                bot->GetName(), targetCreatureEntry, targetPos.x, targetPos.y, targetZ, dist);

                        if (TryWanderMove(bot, movement,
                            DestinationKind::GrindingArea,
                            targetCreatureEntry, 0, targetPos.x, targetPos.y,
                            targetZ, "wander_grinding_area"))
                        {
                            return;
                        }
                    }
                }

                // Priority 4 (Fallback): Forward-Biased Directional Exploration
                static thread_local std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> angleDist(-1.0f, 1.0f);
                std::uniform_real_distribution<float> distDist(15.0f, 35.0f);

                float exploreDist = distDist(rng);
                Position destination = bot->GetFirstCollisionPosition(exploreDist, angleDist(rng));

                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' exploring forward to ({:.1f}, {:.1f}, {:.1f})",
                        bot->GetName(), destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());

                TryWanderMove(bot, movement, DestinationKind::Exploration, 0,
                    0, destination.GetPositionX(), destination.GetPositionY(),
                    destination.GetPositionZ(), "wander_exploration");
                if (_recoveryPolicy.RecordExplorationResult(
                    movement->GetState() != BotMovementState::Idle))
                {
                    _requiresOriginRecovery = true;
                    Finish(ActionOutcome::Blocked,
                        "all productive wander destinations and three bounded exploration cycles failed to start movement",
                        FailureCategory::Navigation,
                        RecoveryDirective::Replan);
                }
            }
        }
    }

    void WanderAction::Stop(Player* /*bot*/, MovementManager* movement)
    {
        if (movement)
        {
            movement->Stop();
        }
    }
}
