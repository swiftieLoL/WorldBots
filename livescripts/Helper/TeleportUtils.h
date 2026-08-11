#pragma once

class Player;

namespace Helper::TeleportUtils
{
    // Socketless bot sessions have no client to acknowledge TeleportTo.
    // Complete the pending transfer on the server so the Player remains
    // registered in the world. Near teleports cannot use the normal packet
    // handler because a socketless session has no active client mover.
    bool CompletePendingTeleport(Player* bot);
}
