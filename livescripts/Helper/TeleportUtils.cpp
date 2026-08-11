#include "TeleportUtils.h"

#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Server/WorldSession.h"

namespace Helper::TeleportUtils
{
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
