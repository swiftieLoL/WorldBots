#pragma once

#include "Player.h"
#include <cstdint>

namespace Helper
{
    class SpellLearningUtils
    {
    public:
        static void AutoLearnClassSpells(Player* bot, uint8_t& lastLearnedLevel);
    };
}
