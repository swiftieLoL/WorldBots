#pragma once

#include "ObjectGuid.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Unit.h"
#include <cstdint>

class Corpse;
class Item;
class SpellCastTargets;

namespace Helper
{
    class SpellUtils
    {
    public:
        // Resolve the highest rank in a TrinityCore spell chain that the bot
        // currently knows. The supplied ID may be any rank in the chain.
        static uint32_t FindHighestKnownRank(Player* bot, uint32_t spellId);

        static uint32_t FindReadyRank(Player* bot, uint32_t spellId);

        // Check if a specific spell is ready to cast (known, off cooldown, enough power, off GCD)
        static bool IsSpellReady(Player* bot, uint32_t spellId);

        static bool HasAuraInChain(Unit* unit, uint32_t spellId, ObjectGuid casterGuid = ObjectGuid::Empty);

        // Safely prepare and cast a spell on target
        static bool TryCast(Player* bot, Unit* target, uint32_t spellId, bool triggered = false);
        static bool TryCastCorpse(Player* bot, Corpse* target, uint32_t spellId, bool triggered = false);

        // Use a condition-resolved quest item through TrinityCore's normal
        // item spell selection. Scripted items and non-primary use spells are
        // deliberately rejected rather than bypassing core item-use rules.
        static bool TryUseResolvedQuestItem(Player* bot, Item* item,
            uint32_t spellId,
            SpellCastTargets const& targets);

        // Returns the class base resurrection spell ID (Priest 2006, Paladin 7328, Shaman 2008, Druid 20484)
        static uint32_t GetClassResurrectionSpell(uint8_t classId);
    };
}
