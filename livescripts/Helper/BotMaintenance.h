#pragma once

#include <cstdint>

class Player;

namespace Helper
{
    class BotMaintenance
    {
    public:
        static void Update(Player* bot, uint8_t& lastLearnedLevel,
            uint8_t& lastProgressionLevel, bool& hunterPetProvisionAttempted);
    };
}
