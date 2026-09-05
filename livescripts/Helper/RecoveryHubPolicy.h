#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Helper::RecoveryHubPolicy
{
    inline constexpr float InnkeeperPreferenceRange = 750.0f;
    // A same-zone teleport must leave the entire local Grind expansion radius
    // before it can honestly be treated as a different ecology.
    inline constexpr float MinimumMaterialEcologyDistance = 900.0f;

    inline bool HasPendingRecovery(bool navigation, bool progression,
        bool townService, bool combatStall, bool flee)
    {
        // Recovery requests must survive normal goal evaluation until the
        // Unstuck action actually runs. In particular, a stalled target can
        // keep the bot in combat and would otherwise immediately preempt the
        // safe-hub relocation that was selected to break that combat loop.
        return navigation || progression || townService || combatStall || flee;
    }

    inline bool IsStarterArea(std::uint32_t mapId, std::uint32_t areaId)
    {
        switch (mapId)
        {
            case 0: // Eastern Kingdoms
                return areaId == 18    // Northshire Valley
                    || areaId == 132   // Coldridge Valley
                    || areaId == 154   // Deathknell / Shadow Grave
                    || areaId == 4342; // Acherus / Scarlet Enclave
            case 1: // Kalimdor
                return areaId == 363   // Valley of Trials
                    || areaId == 220   // Camp Narache
                    || areaId == 358   // Red Cloud Mesa
                    || areaId == 141   // Teldrassil
                    || areaId == 188;  // Shadowglen
            case 530: // Outland / Expansion starter zones
                return areaId == 3526  // Sunstrider Isle
                    || areaId == 3557  // Sunstrider Isle / Falthrien
                    || areaId == 3431  // Ammen Vale
                    || areaId == 3482; // Crash Site / Ammen Vale
            default:
                return false;
        }
    }

    inline bool IsStarterZone(std::uint32_t mapId, std::uint32_t zoneId)
    {
        switch (mapId)
        {
            case 0: // Eastern Kingdoms
                return zoneId == 12    // Elwynn Forest
                    || zoneId == 1     // Dun Morogh
                    || zoneId == 85;   // Tirisfal Glades
            case 1: // Kalimdor
                return zoneId == 14    // Durotar
                    || zoneId == 215   // Mulgore
                    || zoneId == 141;  // Teldrassil
            case 530: // Outland / Expansion starter zones
                return zoneId == 3430  // Eversong Woods
                    || zoneId == 3524; // Azuremyst Isle
            default:
                return false;
        }
    }

    inline bool CanRecoverToHub(std::uint32_t botLevel, std::uint32_t mapId,
        std::uint32_t zoneId, std::uint32_t areaId, bool progressionRecovery)
    {
        if (botLevel >= 6 && IsStarterArea(mapId, areaId))
            return false;
        if (botLevel >= 10 && progressionRecovery && IsStarterZone(mapId, zoneId))
            return false;
        return true;
    }

    inline bool CanUpdateHomebindTo(std::uint32_t botLevel, std::uint32_t currentMap,
        std::uint32_t currentArea, float currentX, float currentY,
        std::uint32_t candidateMap, std::uint32_t candidateArea, float candidateX, float candidateY,
        std::uint32_t candidateZone = 0, std::uint32_t currentZone = 0)
    {
        if (botLevel >= 6 && IsStarterArea(candidateMap, candidateArea))
            return false;
        if (botLevel >= 10 && IsStarterZone(candidateMap, candidateZone))
            return false;
        if (botLevel >= 6 && IsStarterArea(currentMap, currentArea))
            return true;
        if (botLevel >= 10 && IsStarterZone(currentMap, currentZone))
            return true;
        if (currentMap != candidateMap)
            return true;
        float dx = candidateX - currentX;
        float dy = candidateY - currentY;
        return dx * dx + dy * dy >= (1200.0f * 1200.0f);
    }

    inline bool IsZoneLevelSafe(std::uint32_t botLevel, std::uint32_t areaLevel)
    {
        // A zero level is common for capitals and custom areas. In that case
        // local creature ecology, rather than absent DBC metadata, decides.
        return areaLevel == 0 || botLevel >= areaLevel;
    }

    inline bool LevelRangeIntersects(std::uint32_t minimumCreatureLevel,
        std::uint32_t maximumCreatureLevel, std::uint32_t botLevel,
        std::int32_t minimumOffset, std::int32_t maximumOffset)
    {
        std::int32_t low = std::max<std::int32_t>(1,
            static_cast<std::int32_t>(botLevel) + minimumOffset);
        std::int32_t high = std::max<std::int32_t>(low,
            static_cast<std::int32_t>(botLevel) + maximumOffset);
        return static_cast<std::int32_t>(maximumCreatureLevel) >= low &&
            static_cast<std::int32_t>(minimumCreatureLevel) <= high;
    }

    inline bool IsMaterialEcologyChange(std::uint32_t originMapId,
        std::uint32_t originZoneId, float originX, float originY,
        std::uint32_t destinationMapId, std::uint32_t destinationZoneId,
        float destinationX, float destinationY)
    {
        if (originMapId != destinationMapId)
            return true;
        if (originZoneId != 0 && destinationZoneId != 0 &&
            originZoneId != destinationZoneId)
        {
            return true;
        }

        float dx = destinationX - originX;
        float dy = destinationY - originY;
        return dx * dx + dy * dy >=
            MinimumMaterialEcologyDistance * MinimumMaterialEcologyDistance;
    }

    inline bool ShouldPreferCandidate(bool found, bool candidateHasSuitableCreatures,
        bool bestHasSuitableCreatures, bool candidateIsInnkeeper,
        bool bestIsInnkeeper, float candidateDistanceSq, float bestDistanceSq)
    {
        if (!found)
            return true;
        if (candidateHasSuitableCreatures != bestHasSuitableCreatures)
            return candidateHasSuitableCreatures;

        // Inns are stable, faction-safe town destinations. Prefer one over a
        // flight master when it is within a modest detour, but do not cross a
        // continent merely to satisfy that preference.
        if (candidateIsInnkeeper != bestIsInnkeeper)
        {
            float candidateDistance = std::sqrt(candidateDistanceSq);
            float bestDistance = std::sqrt(bestDistanceSq);
            if (candidateIsInnkeeper)
                return candidateDistance <= bestDistance + InnkeeperPreferenceRange;
            return candidateDistance + InnkeeperPreferenceRange < bestDistance;
        }

        return candidateDistanceSq < bestDistanceSq;
    }
}
