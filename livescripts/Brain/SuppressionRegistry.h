#pragma once

#include "Brain/TimedBlacklist.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Brain
{
    struct DangerArea
    {
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        uint32_t untilSec = 0;

        bool Contains(uint32_t candidateMapId, float candidateX, float candidateY,
            uint32_t nowSec) const
        {
            if (candidateMapId != mapId || nowSec >= untilSec)
                return false;
            float dx = candidateX - x;
            float dy = candidateY - y;
            return dx * dx + dy * dy <= radius * radius;
        }

        bool IntersectsSegment(uint32_t candidateMapId, float fromX, float fromY,
            float toX, float toY, uint32_t nowSec) const
        {
            if (candidateMapId != mapId || nowSec >= untilSec)
                return false;
            float segmentX = toX - fromX;
            float segmentY = toY - fromY;
            float lengthSq = segmentX * segmentX + segmentY * segmentY;
            float projection = 0.0f;
            if (lengthSq > 0.001f)
            {
                projection = ((x - fromX) * segmentX + (y - fromY) * segmentY) /
                    lengthSq;
                projection = std::clamp(projection, 0.0f, 1.0f);
            }
            float nearestX = fromX + projection * segmentX;
            float nearestY = fromY + projection * segmentY;
            float dx = nearestX - x;
            float dy = nearestY - y;
            return dx * dx + dy * dy <= radius * radius;
        }

        bool BlocksTravelSegment(uint32_t candidateMapId, float fromX, float fromY,
            float toX, float toY, uint32_t nowSec) const
        {
            if (!IntersectsSegment(candidateMapId, fromX, fromY, toX, toY, nowSec))
                return false;
            if (!Contains(candidateMapId, fromX, fromY, nowSec))
                return true;

            // A bot already inside its personal hazard must be able to leave,
            // but it must not use that exception to continue inward or cross
            // through the centre and emerge on the opposite side.
            float startX = fromX - x;
            float startY = fromY - y;
            float segmentX = toX - fromX;
            float segmentY = toY - fromY;
            float endX = toX - x;
            float endY = toY - y;
            float outwardDot = startX * segmentX + startY * segmentY;
            float startDistanceSq = startX * startX + startY * startY;
            float endDistanceSq = endX * endX + endY * endY;
            return outwardDot < -0.001f ||
                endDistanceSq <= startDistanceSq + 0.001f;
        }
    };

    struct DestinationSuppression
    {
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        uint32_t untilSec = 0;

        bool Contains(uint32_t candidateMapId, float candidateX,
            float candidateY, uint32_t nowSec) const
        {
            if (candidateMapId != mapId || nowSec >= untilSec)
                return false;
            float dx = candidateX - x;
            float dy = candidateY - y;
            return dx * dx + dy * dy <= radius * radius;
        }
    };

    struct QuestDestinationFailureDecision
    {
        uint32_t failureCount = 0;
        uint32_t retryAfterSec = 0;
        bool escalated = false;
    };

    struct QuestDestinationFailure
    {
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        uint32_t failureCount = 0;
        uint32_t lastFailureSec = 0;
    };

    class SuppressionRegistry
    {
    public:
        static constexpr std::size_t MaxDangerAreas = 20;
        static constexpr std::size_t MaxWanderDestinations = 64;
        static constexpr std::size_t MaxGrindDestinations = 64;
        static constexpr std::size_t MaxQuestDestinations = 64;
        static constexpr float QuestDestinationRadius = 80.0f;
        static constexpr uint32_t QuestDestinationRetrySeconds = 30;
        static constexpr uint32_t QuestDestinationFailureWindowSeconds = 10 * 60;
        static constexpr uint32_t QuestDestinationSuppressionSeconds = 15 * 60;
        static constexpr uint32_t QuestDestinationFailureThreshold = 2;
        void SuppressQuest(uint32_t questId, uint32_t untilSec) { _quests.AddUntil(questId, untilSec); }
        void SuppressNpc(uint32_t npcEntry, uint32_t untilSec) { _npcs.AddUntil(npcEntry, untilSec); }
        void SuppressGrindSpawn(uint64_t spawnId, uint32_t untilSec) { _grindSpawns.AddUntil(spawnId, untilSec); }
        void SuppressGrindEntry(uint32_t creatureEntry, uint32_t untilSec) { _grindEntries.AddUntil(creatureEntry, untilSec); }
        void SuppressCombatTarget(uint64_t targetGuid, uint32_t untilSec) { _combatTargets.AddUntil(targetGuid, untilSec); }
        std::size_t PersistGrindAnchorSuppressions(
            const std::unordered_map<uint64_t, uint32_t>& spawnExpiries,
            const std::unordered_map<uint32_t, uint32_t>& entryExpiries,
            uint32_t nowSec)
        {
            std::size_t persisted = 0;
            for (const auto& [spawnId, expirySec] : spawnExpiries)
            {
                if (spawnId != 0 && expirySec > nowSec)
                {
                    _grindSpawns.AddUntil(spawnId, expirySec);
                    ++persisted;
                }
            }
            for (const auto& [entry, expirySec] : entryExpiries)
            {
                if (entry != 0 && expirySec > nowSec)
                {
                    _grindEntries.AddUntil(entry, expirySec);
                    ++persisted;
                }
            }
            return persisted;
        }
        std::size_t PersistWanderDestinationSuppressions(
            const std::vector<DestinationSuppression>& destinations,
            uint32_t nowSec)
        {
            return PersistDestinationSuppressions(destinations,
                _wanderDestinations, MaxWanderDestinations, nowSec);
        }
        std::size_t PersistGrindDestinationSuppressions(
            const std::vector<DestinationSuppression>& destinations,
            uint32_t nowSec)
        {
            return PersistDestinationSuppressions(destinations,
                _grindDestinations, MaxGrindDestinations, nowSec);
        }
        QuestDestinationFailureDecision RecordQuestDestinationFailure(
            uint32_t mapId, float x, float y, uint32_t nowSec)
        {
            auto sameDestination = [=](const auto& candidate) {
                if (candidate.mapId != mapId)
                    return false;
                float dx = candidate.x - x;
                float dy = candidate.y - y;
                return dx * dx + dy * dy <=
                    QuestDestinationRadius * QuestDestinationRadius;
            };

            auto failure = std::find_if(_questDestinationFailures.begin(),
                _questDestinationFailures.end(), sameDestination);
            if (failure == _questDestinationFailures.end())
            {
                _questDestinationFailures.push_back(
                    { mapId, x, y, 0, nowSec });
                if (_questDestinationFailures.size() > MaxQuestDestinations)
                    _questDestinationFailures.erase(
                        _questDestinationFailures.begin());
                failure = _questDestinationFailures.end() - 1;
            }
            else if (nowSec >= failure->lastFailureSec +
                QuestDestinationFailureWindowSeconds)
            {
                failure->failureCount = 0;
                failure->x = x;
                failure->y = y;
            }

            ++failure->failureCount;
            failure->lastFailureSec = nowSec;
            bool escalated = failure->failureCount >=
                QuestDestinationFailureThreshold;
            uint32_t retrySeconds = escalated
                ? QuestDestinationSuppressionSeconds
                : QuestDestinationRetrySeconds;
            uint32_t retryAfterSec = nowSec + retrySeconds;

            auto suppression = std::find_if(_questDestinations.begin(),
                _questDestinations.end(), sameDestination);
            if (suppression == _questDestinations.end())
            {
                _questDestinations.push_back({ mapId, x, y,
                    QuestDestinationRadius, retryAfterSec });
                if (_questDestinations.size() > MaxQuestDestinations)
                    _questDestinations.erase(_questDestinations.begin());
            }
            else
            {
                suppression->untilSec = std::max(suppression->untilSec,
                    retryAfterSec);
                suppression->radius = std::max(suppression->radius,
                    QuestDestinationRadius);
            }

            return { failure->failureCount, retryAfterSec, escalated };
        }
        void RecordQuestDestinationSuccess(uint32_t mapId, float x, float y)
        {
            auto sameDestination = [=](const auto& candidate) {
                if (candidate.mapId != mapId)
                    return false;
                float dx = candidate.x - x;
                float dy = candidate.y - y;
                return dx * dx + dy * dy <=
                    QuestDestinationRadius * QuestDestinationRadius;
            };
            std::erase_if(_questDestinationFailures, sameDestination);
            std::erase_if(_questDestinations, sameDestination);
        }
        void SuppressDangerArea(uint32_t mapId, float x, float y, float radius,
            uint32_t untilSec)
        {
            _dangerAreas.push_back({ mapId, x, y, radius, untilSec });
            if (_dangerAreas.size() > MaxDangerAreas)
                _dangerAreas.erase(_dangerAreas.begin());
        }

        bool IsQuestSuppressed(uint32_t questId, uint32_t nowSec = 0) const
        {
            return nowSec != 0 ? _quests.Contains(questId, nowSec) : _quests.Contains(questId);
        }
        bool IsNpcSuppressed(uint32_t npcEntry, uint32_t nowSec = 0) const
        {
            return nowSec != 0 ? _npcs.Contains(npcEntry, nowSec) : _npcs.Contains(npcEntry);
        }
        bool IsGrindSpawnSuppressed(uint64_t spawnId, uint32_t nowSec = 0) const
        {
            return nowSec != 0 ? _grindSpawns.Contains(spawnId, nowSec) : _grindSpawns.Contains(spawnId);
        }
        bool IsGrindEntrySuppressed(uint32_t creatureEntry, uint32_t nowSec = 0) const
        {
            return nowSec != 0 ? _grindEntries.Contains(creatureEntry, nowSec) : _grindEntries.Contains(creatureEntry);
        }
        bool IsCombatTargetSuppressed(uint64_t targetGuid, uint32_t nowSec = 0) const
        {
            return nowSec != 0 ? _combatTargets.Contains(targetGuid, nowSec) : _combatTargets.Contains(targetGuid);
        }
        bool IsQuestDestinationSuppressed(uint32_t mapId, float x, float y,
            uint32_t nowSec) const
        {
            return std::any_of(_questDestinations.begin(),
                _questDestinations.end(), [=](const DestinationSuppression& destination) {
                    return destination.Contains(mapId, x, y, nowSec);
                });
        }

        bool BlocksTravelSegment(uint32_t fromMapId, float fromX, float fromY,
            uint32_t toMapId, float toX, float toY, uint32_t nowSec) const
        {
            if (fromMapId != toMapId)
                return false;
            return std::any_of(_dangerAreas.begin(), _dangerAreas.end(),
                [=](const DangerArea& area) {
                    return area.BlocksTravelSegment(fromMapId, fromX, fromY,
                        toX, toY, nowSec);
                });
        }

        uint32_t GetQuestSuppressionRemaining(uint32_t questId, uint32_t nowSec) const
        {
            return _quests.GetRemaining(questId, nowSec);
        }

        uint32_t GetNpcSuppressionRemaining(uint32_t npcEntry, uint32_t nowSec) const
        {
            return _npcs.GetRemaining(npcEntry, nowSec);
        }

        std::vector<std::pair<uint32_t, uint32_t>> GetSuppressedQuests(uint32_t nowSec) const
        {
            return _quests.GetRemainingEntries(nowSec);
        }

        void PruneExpired(uint32_t nowSec)
        {
            _quests.Cleanup(nowSec);
            _npcs.Cleanup(nowSec);
            _grindSpawns.Cleanup(nowSec);
            _grindEntries.Cleanup(nowSec);
            _combatTargets.Cleanup(nowSec);
            _dangerAreas.erase(std::remove_if(_dangerAreas.begin(), _dangerAreas.end(),
                [nowSec](const DangerArea& area) { return nowSec >= area.untilSec; }),
                _dangerAreas.end());
            _wanderDestinations.erase(std::remove_if(
                _wanderDestinations.begin(), _wanderDestinations.end(),
                [nowSec](const DestinationSuppression& destination) {
                    return nowSec >= destination.untilSec;
                }), _wanderDestinations.end());
            _grindDestinations.erase(std::remove_if(
                _grindDestinations.begin(), _grindDestinations.end(),
                [nowSec](const DestinationSuppression& destination) {
                    return nowSec >= destination.untilSec;
                }), _grindDestinations.end());
            _questDestinations.erase(std::remove_if(
                _questDestinations.begin(), _questDestinations.end(),
                [nowSec](const DestinationSuppression& destination) {
                    return nowSec >= destination.untilSec;
                }), _questDestinations.end());
            _questDestinationFailures.erase(std::remove_if(
                _questDestinationFailures.begin(),
                _questDestinationFailures.end(),
                [nowSec](const QuestDestinationFailure& failure) {
                    return nowSec >= failure.lastFailureSec +
                        QuestDestinationFailureWindowSeconds;
                }), _questDestinationFailures.end());
        }

        const std::unordered_map<uint32_t, uint32_t>& GetBlacklistedQuests() const { return _quests.Entries(); }
        const std::unordered_map<uint32_t, uint32_t>& GetBlacklistedNpcs() const { return _npcs.Entries(); }
        const std::unordered_map<uint64_t, uint32_t>& GetBlacklistedGrindSpawns() const { return _grindSpawns.Entries(); }
        const std::unordered_map<uint32_t, uint32_t>& GetBlacklistedGrindEntries() const { return _grindEntries.Entries(); }
        const std::vector<DangerArea>& GetDangerAreas() const { return _dangerAreas; }
        const std::vector<DestinationSuppression>& GetWanderDestinationSuppressions() const
        {
            return _wanderDestinations;
        }
        const std::vector<DestinationSuppression>& GetGrindDestinationSuppressions() const
        {
            return _grindDestinations;
        }
        const std::vector<DestinationSuppression>& GetQuestDestinationSuppressions() const
        {
            return _questDestinations;
        }
        const std::vector<QuestDestinationFailure>& GetQuestDestinationFailures() const
        {
            return _questDestinationFailures;
        }

    private:
        static std::size_t PersistDestinationSuppressions(
            const std::vector<DestinationSuppression>& destinations,
            std::vector<DestinationSuppression>& registry,
            std::size_t maximumDestinations, uint32_t nowSec)
        {
            std::size_t changed = 0;
            for (const DestinationSuppression& destination : destinations)
            {
                if (destination.radius <= 0.0f ||
                    destination.untilSec <= nowSec)
                {
                    continue;
                }

                auto existing = std::find_if(registry.begin(), registry.end(),
                    [&](const DestinationSuppression& candidate) {
                        return candidate.mapId == destination.mapId &&
                            candidate.Contains(destination.mapId,
                                destination.x, destination.y, nowSec);
                    });
                if (existing != registry.end())
                {
                    if (destination.untilSec > existing->untilSec)
                    {
                        existing->untilSec = destination.untilSec;
                        existing->radius = std::max(existing->radius,
                            destination.radius);
                        ++changed;
                    }
                    continue;
                }

                registry.push_back(destination);
                ++changed;
                if (registry.size() > maximumDestinations)
                    registry.erase(registry.begin());
            }
            return changed;
        }

        TimedBlacklist<uint32_t> _quests;
        TimedBlacklist<uint32_t> _npcs;
        TimedBlacklist<uint64_t> _grindSpawns;
        TimedBlacklist<uint32_t> _grindEntries;
        TimedBlacklist<uint64_t> _combatTargets;
        std::vector<DangerArea> _dangerAreas;
        std::vector<DestinationSuppression> _wanderDestinations;
        std::vector<DestinationSuppression> _grindDestinations;
        std::vector<DestinationSuppression> _questDestinations;
        std::vector<QuestDestinationFailure> _questDestinationFailures;
    };
}
