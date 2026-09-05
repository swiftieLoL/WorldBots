#include "TeleportUtils.h"

#include "Entities/Creature/Creature.h"
#include "DataStores/DBCStores.h"
#include "Helper/MovementPathPolicy.h"
#include "Helper/RecoveryHubPolicy.h"
#include "Log.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Server/WorldSession.h"

#include <array>
#include <cmath>

namespace Helper::TeleportUtils
{
    bool HasCompleteGroundPathTo(Player* bot, float x, float y, float z)
    {
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return false;

        PathGenerator path(bot);
        path.SetUseStraightPath(false);
        bool calculated = path.CalculatePath(x, y, z);
        uint32 flags = static_cast<uint32>(path.GetPathType());
        if (Helper::MovementPathPolicy::IsCompleteGroundPath(
            calculated, flags, path.GetPath().size()))
        {
            return true;
        }

        if (path.GetPath().empty())
            return false;
        const G3D::Vector3& normalized = path.GetPath().back();
        if (!Helper::MovementPathPolicy::IsUsableNormalizedEndpoint(
            calculated, flags, path.GetPath().size(), { x, y, z },
            { normalized.x, normalized.y, normalized.z }))
        {
            return false;
        }

        // The first query only identifies a tightly bounded surface
        // correction. This second query proves that the corrected point is a
        // complete ground destination before preflight accepts it.
        PathGenerator correctedPath(bot);
        correctedPath.SetUseStraightPath(false);
        bool correctedCalculated = correctedPath.CalculatePath(
            normalized.x, normalized.y, normalized.z);
        uint32 correctedFlags =
            static_cast<uint32>(correctedPath.GetPathType());
        return Helper::MovementPathPolicy::IsCompleteGroundPath(
            correctedCalculated, correctedFlags,
            correctedPath.GetPath().size());
    }

    bool HasUsableGroundOrigin(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return false;

        constexpr std::array<float, 8> ProbeAngles = {
            0.0f, 0.7853982f, 1.5707963f, 2.3561945f,
            3.1415927f, 3.9269908f, 4.7123890f, 5.4977871f
        };
        constexpr float ProbeDistance = 8.0f;
        for (float angle : ProbeAngles)
        {
            PathGenerator path(bot);
            path.SetUseStraightPath(false);
            bool calculated = path.CalculatePath(
                bot->GetPositionX() + std::cos(angle) * ProbeDistance,
                bot->GetPositionY() + std::sin(angle) * ProbeDistance,
                bot->GetPositionZ());
            uint32 flags = static_cast<uint32>(path.GetPathType());
            if (Helper::MovementPathPolicy::IsCompleteGroundPath(
                    calculated, flags, path.GetPath().size()) ||
                Helper::MovementPathPolicy::IsUsableTravelFrontier(
                    calculated, flags, path.GetPath().size()))
            {
                return true;
            }
        }
        return false;
    }

    bool TryRelocateToLocalNavmesh(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return false;

        float originalX = bot->GetPositionX();
        float originalY = bot->GetPositionY();
        float originalZ = bot->GetPositionZ();
        float originalO = bot->GetOrientation();
        PathGenerator path(bot);
        path.SetUseStraightPath(false);
        bool calculated = path.CalculatePath(originalX, originalY, originalZ);
        uint32 flags = static_cast<uint32>(path.GetPathType());
        if (!calculated ||
            (flags & Helper::MovementPathPolicy::FarFromPolyStart) == 0 ||
            path.GetPath().size() < 2)
        {
            return false;
        }

        const G3D::Vector3& candidate = path.GetActualEndPosition();
        float horizontalDx = candidate.x - originalX;
        float horizontalDy = candidate.y - originalY;
        float horizontalDistance = std::sqrt(
            horizontalDx * horizontalDx + horizontalDy * horizontalDy);
        float verticalDistance = std::fabs(candidate.z - originalZ);
        constexpr float MaximumLocalHorizontalCorrection = 4.0f;
        constexpr float MaximumLocalVerticalCorrection = 50.0f;
        if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) ||
            !std::isfinite(candidate.z) ||
            horizontalDistance > MaximumLocalHorizontalCorrection ||
            verticalDistance > MaximumLocalVerticalCorrection)
        {
            return false;
        }

        constexpr float RecoveryHeightOffset = 0.5f;
        bot->NearTeleportTo(candidate.x, candidate.y,
            candidate.z + RecoveryHeightOffset, originalO);
        if (!CompletePendingTeleport(bot) || !HasUsableGroundOrigin(bot))
        {
            bot->NearTeleportTo(originalX, originalY, originalZ, originalO);
            CompletePendingTeleport(bot);
            return false;
        }

        TC_LOG_INFO("server", "[WorldBots] [Recovery] Bot '{}' locally corrected its navmesh layer from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f}) and validated ground movement",
            bot->GetName(), originalX, originalY, originalZ,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        return true;
    }

    bool IsStarterArea(uint32 mapId, uint32 areaId)
    {
        return Helper::RecoveryHubPolicy::IsStarterArea(mapId, areaId);
    }

    bool IsHomebindInStarterArea(Player* bot)
    {
        if (!bot)
            return false;
        return IsStarterArea(bot->m_homebindMapId, bot->m_homebindAreaId);
    }

    bool IsHomebindInStarterZone(Player* bot)
    {
        if (!bot)
            return false;
        uint32 areaId = bot->m_homebindAreaId;
        AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(areaId);
        uint32 zoneId = (areaEntry && areaEntry->ParentAreaID != 0) ? areaEntry->ParentAreaID : areaId;
        return Helper::RecoveryHubPolicy::IsStarterZone(bot->m_homebindMapId, zoneId);
    }

    void SetHomebind(Player* bot, uint32 mapId, uint32 areaId, float x, float y, float z)
    {
        if (!bot)
            return;

        bot->SetHomebind(WorldLocation(mapId, x, y, z), areaId);
        bot->SaveToDB();
        TC_LOG_INFO("server", "[WorldBots] [TeleportUtils] Bot '{}' updated homebind to Map {} Area {} ({:.1f}, {:.1f}, {:.1f})",
            bot->GetName(), mapId, areaId, x, y, z);
    }

    bool ShouldUpdateHomebind(Player* bot, Creature* innkeeper)
    {
        if (!bot || !innkeeper)
            return false;

        AreaTableEntry const* currentArea = sAreaTableStore.LookupEntry(bot->m_homebindAreaId);
        uint32 currentZoneId = (currentArea && currentArea->ParentAreaID != 0) ? currentArea->ParentAreaID : bot->m_homebindAreaId;
        AreaTableEntry const* candArea = sAreaTableStore.LookupEntry(innkeeper->GetAreaId());
        uint32 candZoneId = (candArea && candArea->ParentAreaID != 0) ? candArea->ParentAreaID : innkeeper->GetAreaId();

        return Helper::RecoveryHubPolicy::CanUpdateHomebindTo(
            bot->GetLevel(), bot->m_homebindMapId, bot->m_homebindAreaId,
            bot->m_homebindX, bot->m_homebindY,
            innkeeper->GetMapId(), innkeeper->GetAreaId(),
            innkeeper->GetPositionX(), innkeeper->GetPositionY(),
            candZoneId, currentZoneId);
    }

    void TeleportToHomebind(Player* bot)
    {
        if (!bot) return;
        if (bot->m_homebindX != 0.0f)
            bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ, bot->GetOrientation());
        else
            bot->TeleportTo(bot->GetStartPosition());
        CompletePendingTeleport(bot);
    }

    bool CompletePendingTeleport(Player* bot)
    {
        if (!bot)
            return false;

        if (bot->IsBeingTeleportedFar())
        {
            // The far-teleport handler is already entirely server driven and
            // does not require a movement ACK packet from a client.
            WorldSession* session = bot->GetSession();
            if (!session)
                return false;
            session->HandleMoveWorldportAck();
        }
        else if (bot->IsBeingTeleportedNear())
        {
            // WorldSession::HandleMoveTeleportAck validates the session's
            // actively moved client unit before it clears this semaphore.
            // Socketless bot sessions do not have that client movement state,
            // so complete the same core steps directly on the server.
            uint32 oldZone = bot->GetZoneId();
            WorldLocation const destination = bot->GetTeleportDest();

            bot->SetSemaphoreTeleportNear(false);
            bot->UpdatePosition(destination, true);
            bot->SetFallInformation(0, bot->GetPositionZ());

            uint32 newZone = 0;
            uint32 newArea = 0;
            bot->GetZoneAndAreaId(newZone, newArea);
            bot->UpdateZone(newZone, newArea);

            if (oldZone != newZone)
            {
                if (bot->pvpInfo.IsHostile)
                    bot->CastSpell(bot, 2479, true);
                else if (bot->IsPvP() && !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
                    bot->UpdatePvP(false, false);
            }

            bot->ResummonPetTemporaryUnSummonedIfAny();
            bot->ProcessDelayedOperations();
        }

        return !bot->IsBeingTeleported();
    }
}
