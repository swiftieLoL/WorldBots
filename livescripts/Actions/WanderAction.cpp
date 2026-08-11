#include "WanderAction.h"
#include "Blackboard/BotBlackboard.h"
#include "Cache/BotCache.h"
#include "Map.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include <random>
#include <ctime>
#include <unordered_set>

namespace Actions
{
    WanderAction::WanderAction(float originX, float originY, float originZ, float radius,
        std::unordered_map<uint32_t, uint32_t> suppressedQuests)
        : _originX(originX), _originY(originY), _originZ(originZ), _radius(radius), _searchRadius(radius), _pauseTimer(0),
          _suppressedQuests(std::move(suppressedQuests))
    {
    }

    void WanderAction::Start(Player* /*bot*/, MovementManager* movement)
    {
        if (!movement) return;
        _pauseTimer = 0;
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
                uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
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
                        if (Map* map = bot->GetMap())
                        {
                            float floorZ = map->GetHeight(bot->GetPhaseMask(), targetPos.x, targetPos.y, targetPos.z, true, 50.0f);
                            if (floorZ > -500.0f && !std::isnan(floorZ)) targetZ = floorZ;
                        }

                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' wandering productively towards Quest Starter {} Entry {} (Quest {}) at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                                bot->GetName(), targetIsGameObject ? "GameObject" : "NPC", targetNpcEntry, targetQuestId, targetPos.x, targetPos.y, targetZ, dist);

                        movement->MoveTo(targetPos.x, targetPos.y, targetZ, BotMovementState::Moving, false);
                        return;
                    }

                    // Give the one-second blackboard scan time to discover the
                    // nearby starter. Do not immediately replace this goal with
                    // a settlement destination at the end of a route segment.
                    return;
                }

                // Priority 2: Search for a Settlement / Vendor Hub on this map
                uint32_t targetVendorEntry = 0;
                if (Cache::BotCache::FindNearestSettlementOrVendor(mapId, curX, curY, curZ, 40.0f, targetPos, targetVendorEntry))
                {
                    float dist = Helper::Distance2D(curX, curY, targetPos.x, targetPos.y);
                    if (dist > 25.0f)
                    {
                        float targetZ = targetPos.z;
                        if (Map* map = bot->GetMap())
                        {
                            float floorZ = map->GetHeight(bot->GetPhaseMask(), targetPos.x, targetPos.y, targetPos.z, true, 50.0f);
                            if (floorZ > -500.0f && !std::isnan(floorZ)) targetZ = floorZ;
                        }

                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' wandering productively towards Settlement/Vendor Entry {} at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                                bot->GetName(), targetVendorEntry, targetPos.x, targetPos.y, targetZ, dist);

                        movement->MoveTo(targetPos.x, targetPos.y, targetZ, BotMovementState::Moving, false);
                        return;
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
                        if (Map* map = bot->GetMap())
                        {
                            float floorZ = map->GetHeight(bot->GetPhaseMask(), targetPos.x, targetPos.y, targetPos.z, true, 50.0f);
                            if (floorZ > -500.0f && !std::isnan(floorZ)) targetZ = floorZ;
                        }

                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Wander] Bot '{}' wandering productively towards Level-Appropriate Mobs (Entry {}) at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                                bot->GetName(), targetCreatureEntry, targetPos.x, targetPos.y, targetZ, dist);

                        movement->MoveTo(targetPos.x, targetPos.y, targetZ, BotMovementState::Moving, false);
                        return;
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

                movement->MoveTo(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ(),
                    BotMovementState::Moving, false);
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
