#pragma once

class Player;
class Creature;

namespace Helper::TeleportUtils
{
    void TeleportToHomebind(Player* bot);
    void SetHomebind(Player* bot, uint32 mapId, uint32 areaId, float x, float y, float z);
    bool IsStarterArea(uint32 mapId, uint32 areaId);
    bool IsHomebindInStarterArea(Player* bot);
    bool IsHomebindInStarterZone(Player* bot);
    bool ShouldUpdateHomebind(Player* bot, Creature* innkeeper);
    bool HasUsableGroundOrigin(Player* bot);
    bool HasCompleteGroundPathTo(Player* bot, float x, float y, float z);
    bool TryRelocateToLocalNavmesh(Player* bot);

    // Socketless bot sessions have no client to acknowledge TeleportTo.
    // Complete the pending transfer on the server so the Player remains
    // registered in the world. Near teleports cannot use the normal packet
    // handler because a socketless session has no active client mover.
    bool CompletePendingTeleport(Player* bot);
}

