#pragma once

#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Unit.h"
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace Helper
{
    class SpellUtils
    {
    public:
        // Find the first known spell ID from a priority array/list
        static uint32_t FindKnownSpell(Player* bot, std::span<const uint32_t> spellIds);
        static uint32_t FindKnownSpell(Player* bot, const std::vector<uint32_t>& spellIds);

        // Resolve the highest rank in a TrinityCore spell chain that the bot
        // currently knows. The supplied ID may be any rank in the chain.
        static uint32_t FindHighestKnownRank(Player* bot, uint32_t spellId);

        // Find the first spell from a priority list that is known AND off cooldown/GCD
        static uint32_t FindReadySpell(Player* bot, std::span<const uint32_t> spellIds);
        static uint32_t FindReadyRank(Player* bot, uint32_t spellId);

        // Check if a specific spell is ready to cast (known, off cooldown, enough power, off GCD)
        static bool IsSpellReady(Player* bot, uint32_t spellId);

        // Check if a unit has any aura from a given list of spell IDs
        static bool HasAnyAura(Unit* unit, std::span<const uint32_t> spellIds);
        static bool HasAuraInChain(Unit* unit, uint32_t spellId);

        // Safely prepare and cast a spell on target
        static bool TryCast(Player* bot, Unit* target, uint32_t spellId, bool triggered = false);
    };
}
