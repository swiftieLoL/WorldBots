#include "SpellUtils.h"
#include "ByteBuffer.h"
#include "Corpse.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellHistory.h"
#include "Item.h"
#include "QuestItemUsePolicy.h"

namespace Helper
{
    uint32_t SpellUtils::FindHighestKnownRank(Player* bot, uint32_t spellId)
    {
        if (!bot || !spellId)
            return 0;

        uint32_t first = sSpellMgr->GetFirstSpellInChain(spellId);
        if (first == 0)
            return bot->HasSpell(spellId) ? spellId : 0;

        uint32_t knownRank = 0;
        for (uint32_t current = first;
             current != 0; current = sSpellMgr->GetNextSpellInChain(current))
        {
            if (bot->HasSpell(current))
                knownRank = current;
        }
        return knownRank;
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

    bool SpellUtils::HasAuraInChain(Unit* unit, uint32_t spellId, ObjectGuid casterGuid)
    {
        if (!unit || !spellId)
            return false;

        bool filterByCaster = !casterGuid.IsEmpty();
        uint32_t first = sSpellMgr->GetFirstSpellInChain(spellId);
        if (first == 0)
            return filterByCaster ? unit->HasAura(spellId, casterGuid) : unit->HasAura(spellId);

        for (uint32_t current = first;
             current != 0; current = sSpellMgr->GetNextSpellInChain(current))
        {
            if (filterByCaster ? unit->HasAura(current, casterGuid) : unit->HasAura(current))
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

    bool SpellUtils::TryCastCorpse(Player* bot, Corpse* target, uint32_t spellId, bool triggered)
    {
        if (!bot || !target || !spellId) return false;

        if (bot->HasUnitState(UNIT_STATE_CASTING))
            return false;

        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info) return false;

        TriggerCastFlags flags = triggered ? TRIGGERED_FULL_MASK : TRIGGERED_NONE;
        Spell* spell = new Spell(bot, info, flags);
        SpellCastTargets targets;
        ByteBuffer data;
        data << uint32_t(TARGET_FLAG_CORPSE_ALLY);
        data << target->GetGUID().WriteAsPacked();
        targets.Read(data, bot);
        return spell->prepare(targets) == SPELL_CAST_OK;
    }

    bool SpellUtils::TryUseResolvedQuestItem(Player* bot, Item* item,
        uint32_t spellId,
        SpellCastTargets const& targets)
    {
        if (!bot || !item || spellId == 0 ||
            item->GetOwnerGUID() != bot->GetGUID() ||
            bot->GetItemByGuid(item->GetGUID()) != item ||
            bot->GetUseableItemByPos(item->GetBagSlot(), item->GetSlot()) != item)
        {
            return false;
        }

        ItemTemplate const* itemTemplate = item->GetTemplate();
        if (!itemTemplate ||
            (itemTemplate->InventoryType != INVTYPE_NON_EQUIP &&
             !item->IsEquipped()) ||
            bot->CanUseItem(item) != EQUIP_ERR_OK)
        {
            return false;
        }

        if (bot->InArena() &&
            ((itemTemplate->Class == ITEM_CLASS_CONSUMABLE &&
              !itemTemplate->HasFlag(ITEM_FLAG_IGNORE_DEFAULT_ARENA_RESTRICTIONS)) ||
             itemTemplate->HasFlag(ITEM_FLAG_NOT_USEABLE_IN_ARENA)))
        {
            return false;
        }

        std::array<QuestItemUseSpellSlot, MAX_ITEM_PROTO_SPELLS> useSpellSlots{};
        for (uint8_t spellIndex = 0;
            spellIndex < MAX_ITEM_PROTO_SPELLS; ++spellIndex)
        {
            const auto& itemSpell = itemTemplate->Spells[spellIndex];
            uint32_t authoredSpellId = itemSpell.SpellId > 0
                ? static_cast<uint32_t>(itemSpell.SpellId) : 0;
            useSpellSlots[spellIndex] = {
                authoredSpellId,
                itemSpell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE,
                authoredSpellId != 0 &&
                    sSpellMgr->GetSpellInfo(authoredSpellId) != nullptr
            };
        }
        uint32_t firstOnUseSpellId =
            SelectFirstValidQuestItemUseSpell(useSpellSlots);

        if (!IsSupportedQuestItemUse(itemTemplate->ScriptId,
            firstOnUseSpellId, spellId))
        {
            return false;
        }

        // Match HandleUseItemOpcode's conservative combat preflight: if any
        // authored item spell is forbidden in combat, the item is not used.
        if (bot->IsInCombat())
        {
            for (uint8_t spellIndex = 0;
                spellIndex < MAX_ITEM_PROTO_SPELLS; ++spellIndex)
            {
                if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(
                    itemTemplate->Spells[spellIndex].SpellId))
                {
                    if (!spellInfo->CanBeUsedInCombat())
                        return false;
                }
            }
        }

        if (!sSpellMgr->GetSpellInfo(spellId))
            return false;

        if ((itemTemplate->Bonding == BIND_WHEN_USE ||
             itemTemplate->Bonding == BIND_WHEN_PICKED_UP ||
             itemTemplate->Bonding == BIND_QUEST_ITEM) && !item->IsSoulBound())
        {
            item->SetState(ITEM_CHANGED, bot);
            item->SetBinding(true);
        }

        bot->CastItemUseSpell(item, targets, 0, 0);
        return true;
    }

    uint32_t SpellUtils::GetClassResurrectionSpell(uint8_t classId)
    {
        switch (classId)
        {
            case CLASS_PRIEST: return 2006;  // Resurrection
            case CLASS_PALADIN: return 7328; // Redemption
            case CLASS_SHAMAN: return 2008;  // Ancestral Spirit
            case CLASS_DRUID: return 50769;  // Revive (WotLK out-of-combat resurrection)
            default: return 0;
        }
    }
}
