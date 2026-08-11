#include "Globals/ObjectMgr.h"
#include "SpellUtils.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellHistory.h"

namespace Helper
{
    uint32_t SpellUtils::FindKnownSpell(Player* bot, std::span<const uint32_t> spellIds)
    {
        if (!bot) return 0;

        for (uint32_t sId : spellIds)
        {
            if (bot->HasSpell(sId))
                return sId;
        }

        return 0;
    }

    uint32_t SpellUtils::FindKnownSpell(Player* bot, const std::vector<uint32_t>& spellIds)
    {
        return FindKnownSpell(bot, std::span<const uint32_t>(spellIds.data(), spellIds.size()));
    }

    uint32_t SpellUtils::FindHighestKnownRank(Player* bot, uint32_t spellId)
    {
        if (!bot || !spellId)
            return 0;

        uint32_t knownRank = 0;
        for (uint32_t current = sSpellMgr->GetFirstSpellInChain(spellId);
             current != 0; current = sSpellMgr->GetNextSpellInChain(current))
        {
            if (bot->HasSpell(current))
                knownRank = current;
        }
        return knownRank;
    }

    uint32_t SpellUtils::FindReadySpell(Player* bot, std::span<const uint32_t> spellIds)
    {
        if (!bot) return 0;

        for (uint32_t sId : spellIds)
        {
            if (bot->HasSpell(sId) && IsSpellReady(bot, sId))
                return sId;
        }

        return 0;
    }

    uint32_t SpellUtils::FindReadyRank(Player* bot, uint32_t spellId)
    {
        uint32_t knownRank = FindHighestKnownRank(bot, spellId);
        return knownRank != 0 && IsSpellReady(bot, knownRank) ? knownRank : 0;
    }

    bool SpellUtils::IsSpellReady(Player* bot, uint32_t spellId)
    {
        if (!bot || !spellId) return false;

        if (bot->GetSpellHistory() && bot->GetSpellHistory()->HasCooldown(spellId))
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return false;

        if (spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask()) > bot->GetPower(bot->GetPowerType()))
            return false;

        if (bot->GetSpellHistory() && bot->GetSpellHistory()->HasGlobalCooldown(spellInfo))
            return false;

        return true;
    }

    bool SpellUtils::HasAnyAura(Unit* unit, std::span<const uint32_t> spellIds)
    {
        if (!unit) return false;

        for (uint32_t sId : spellIds)
        {
            if (sId > 0 && unit->HasAura(sId))
                return true;
        }
        return false;
    }

    bool SpellUtils::HasAuraInChain(Unit* unit, uint32_t spellId)
    {
        if (!unit || !spellId)
            return false;

        for (uint32_t current = sSpellMgr->GetFirstSpellInChain(spellId);
             current != 0; current = sSpellMgr->GetNextSpellInChain(current))
        {
            if (unit->HasAura(current))
                return true;
        }
        return false;
    }

    bool SpellUtils::TryCast(Player* bot, Unit* target, uint32_t spellId, bool triggered)
    {
        if (!bot || !spellId) return false;

        if (!target)
            target = bot;

        if (bot->HasUnitState(UNIT_STATE_CASTING))
            return false;

        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info) return false;

        TriggerCastFlags flags = triggered ? TRIGGERED_FULL_MASK : TRIGGERED_NONE;
        Spell* spell = new Spell(bot, info, flags);
        SpellCastTargets targets;
        targets.SetUnitTarget(target);
        return spell->prepare(targets) == SPELL_CAST_OK;
    }
}
