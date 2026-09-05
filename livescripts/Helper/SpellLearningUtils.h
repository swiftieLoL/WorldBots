#pragma once

#include <cstdint>

class Player;

namespace Helper
{
    class SpellLearningUtils
    {
    public:
        static void AutoLearnClassSpells(Player* bot, uint8_t& lastLearnedLevel);
    };
}
