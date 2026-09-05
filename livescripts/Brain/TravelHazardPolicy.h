#pragma once

#include "Brain/SuppressionRegistry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Brain
{
    // The active destination of proactive world travel. Quests and vendor
    // visits use different suppression namespaces, so observing one must
    // replace any stale destination of the other kind.
    class ProactiveRouteTarget
    {
    public:
        void Observe(uint32_t questId, uint32_t npcEntry)
        {
            if (questId != 0)
            {
                _questId = questId;
                _npcEntry = 0;
            }
            else if (npcEntry != 0)
            {
                _questId = 0;
                _npcEntry = npcEntry;
            }
        }

        void Clear()
        {
            _questId = 0;
            _npcEntry = 0;
        }

        bool HasTarget() const { return _questId != 0 || _npcEntry != 0; }
        uint32_t GetQuestId() const { return _questId; }
        uint32_t GetNpcEntry() const { return _npcEntry; }

    private:
        uint32_t _questId = 0;
        uint32_t _npcEntry = 0;
    };

    // Detects repeated proactive routes through distinct unsafe areas. Several
    // enemies chained together in one corridor are one route lesson, not
    // several independent reasons to relocate the bot.
    class TravelHazardPolicy
    {
    public:
        static constexpr uint32_t HazardWindowSeconds = 180;
        static constexpr std::size_t HazardRecoveryThreshold = 3;
        static constexpr float SameHazardAreaRadius = 90.0f;
        static constexpr std::size_t MaxPersonalDangerAreas = 20;

        void SuppressDangerArea(uint32_t mapId, float x, float y, float radius,
            uint32_t untilSec)
        {
            _personalDangerAreas.push_back({ mapId, x, y, radius, untilSec });
            if (_personalDangerAreas.size() > MaxPersonalDangerAreas)
                _personalDangerAreas.erase(_personalDangerAreas.begin());
        }

        void PruneExpired(uint32_t nowSec)
        {
            _personalDangerAreas.erase(
                std::remove_if(_personalDangerAreas.begin(), _personalDangerAreas.end(),
                    [nowSec](const DangerArea& area) {
                        return nowSec >= area.untilSec;
                    }),
                _personalDangerAreas.end());
            _unsafeRouteIncidents.erase(
                std::remove_if(_unsafeRouteIncidents.begin(), _unsafeRouteIncidents.end(),
                    [nowSec](const UnsafeRouteIncident& incident) {
                        return (nowSec - incident.timestamp) > HazardWindowSeconds;
                    }),
                _unsafeRouteIncidents.end());
        }

        const std::vector<DangerArea>& GetDangerAreas() const
        {
            return _personalDangerAreas;
        }

        bool RecordUnsafeRoute(uint32_t nowSec, uint32_t mapId, float x, float y)
        {
            _unsafeRouteIncidents.erase(
                std::remove_if(_unsafeRouteIncidents.begin(), _unsafeRouteIncidents.end(),
                    [nowSec](const UnsafeRouteIncident& incident) {
                        return (nowSec - incident.timestamp) > HazardWindowSeconds;
                    }),
                _unsafeRouteIncidents.end());

            float sameAreaRadiusSq = SameHazardAreaRadius * SameHazardAreaRadius;
            auto sameArea = std::find_if(_unsafeRouteIncidents.begin(),
                _unsafeRouteIncidents.end(),
                [=](const UnsafeRouteIncident& incident) {
                    float dx = x - incident.x;
                    float dy = y - incident.y;
                    return incident.mapId == mapId &&
                        dx * dx + dy * dy <= sameAreaRadiusSq;
                });
            if (sameArea != _unsafeRouteIncidents.end())
            {
                sameArea->timestamp = nowSec;
                return false;
            }

            _unsafeRouteIncidents.push_back({ nowSec, mapId, x, y });

            if (_unsafeRouteIncidents.size() < HazardRecoveryThreshold)
                return false;

            _unsafeRouteIncidents.clear();
            return true;
        }

        std::size_t GetRecentUnsafeRouteCount() const
        {
            return _unsafeRouteIncidents.size();
        }

    private:
        struct UnsafeRouteIncident
        {
            uint32_t timestamp;
            uint32_t mapId;
            float x;
            float y;
        };

        std::vector<UnsafeRouteIncident> _unsafeRouteIncidents;
        std::vector<DangerArea> _personalDangerAreas;
    };
}
