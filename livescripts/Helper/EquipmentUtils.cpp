#include "EquipmentUtils.h"

#include "Globals/ObjectMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Helper
{
    namespace
    {
        struct StatWeights
        {
            float strength = 0.0f;
            float agility = 0.0f;
            float stamina = 0.35f;
            float intellect = 0.0f;
            float spirit = 0.0f;
            float attackPower = 0.0f;
            float rangedAttackPower = 0.0f;
            float spellPower = 0.0f;
            float hit = 0.0f;
            float crit = 0.0f;
            float haste = 0.0f;
            float expertise = 0.0f;
            float armorPenetration = 0.0f;
            float manaPerFive = 0.0f;
            float defense = 0.0f;
            float weaponDps = 0.0f;
            float rangedDps = 0.0f;
        };

        StatWeights WeightsFor(BotSpecialization specialization)
        {
            switch (specialization)
            {
                case BotSpecialization::Arms:
                case BotSpecialization::Blood:
                case BotSpecialization::Retribution:
                    return { 2.1f, 0.7f, 0.65f, 0.15f, 0.0f, 1.0f, 0.2f, 0.05f,
                        1.5f, 1.25f, 1.1f, 1.35f, 1.25f, 0.1f, 0.15f, 8.0f, 0.5f };
                case BotSpecialization::Combat:
                    return { 1.05f, 2.15f, 0.55f, 0.0f, 0.0f, 1.0f, 0.2f, 0.0f,
                        1.7f, 1.35f, 1.45f, 1.45f, 1.35f, 0.0f, 0.0f, 8.5f, 0.5f };
                case BotSpecialization::Marksmanship:
                    return { 0.15f, 2.35f, 0.55f, 0.55f, 0.0f, 0.65f, 1.0f, 0.0f,
                        1.8f, 1.55f, 1.45f, 0.0f, 1.45f, 0.0f, 0.0f, 1.5f, 11.0f };
                case BotSpecialization::Enhancement:
                    return { 1.75f, 1.9f, 0.6f, 0.45f, 0.0f, 1.0f, 0.25f, 0.25f,
                        1.55f, 1.35f, 1.35f, 1.2f, 1.1f, 0.2f, 0.0f, 7.5f, 0.5f };
                case BotSpecialization::Feral:
                    return { 1.7f, 2.15f, 0.7f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                        1.4f, 1.4f, 1.25f, 1.3f, 1.2f, 0.0f, 0.0f, 1.0f, 0.0f };
                case BotSpecialization::Shadow:
                case BotSpecialization::Frost:
                case BotSpecialization::Affliction:
                    return { 0.0f, 0.0f, 0.45f, 1.05f, 0.65f, 0.0f, 0.0f, 1.75f,
                        1.45f, 1.25f, 1.35f, 0.0f, 0.0f, 1.2f, 0.0f, 0.15f, 0.8f };
                case BotSpecialization::Generic:
                default:
                    return { 1.0f, 1.0f, 0.6f, 0.4f, 0.2f, 0.5f, 0.5f, 0.5f,
                        0.5f, 0.5f, 0.5f, 0.3f, 0.3f, 0.2f, 0.1f, 4.0f, 4.0f };
            }
        }

        bool IsRangedWeapon(ItemTemplate const* itemTemplate)
        {
            if (!itemTemplate)
                return false;

            return itemTemplate->InventoryType == INVTYPE_RANGED ||
                itemTemplate->InventoryType == INVTYPE_RANGEDRIGHT ||
                itemTemplate->InventoryType == INVTYPE_THROWN;
        }

        bool IsTwoHanded(ItemTemplate const* itemTemplate)
        {
            return itemTemplate && itemTemplate->InventoryType == INVTYPE_2HWEAPON;
        }

        float WeaponDps(ItemTemplate const* itemTemplate)
        {
            if (!itemTemplate || itemTemplate->Class != ITEM_CLASS_WEAPON || itemTemplate->Delay == 0)
                return 0.0f;

            float damage = 0.0f;
            for (auto const& entry : itemTemplate->Damage)
                damage += (entry.DamageMin + entry.DamageMax) * 0.5f;
            return damage * 1000.0f / static_cast<float>(itemTemplate->Delay);
        }

        float RatingWeight(StatWeights const& weights, uint32_t statType)
        {
            switch (statType)
            {
                case ITEM_MOD_MANA: return weights.intellect * 0.05f;
                case ITEM_MOD_HEALTH: return weights.stamina * 0.05f;
                case ITEM_MOD_AGILITY: return weights.agility;
                case ITEM_MOD_STRENGTH: return weights.strength;
                case ITEM_MOD_INTELLECT: return weights.intellect;
                case ITEM_MOD_SPIRIT: return weights.spirit;
                case ITEM_MOD_STAMINA: return weights.stamina;
                case ITEM_MOD_DEFENSE_SKILL_RATING:
                case ITEM_MOD_DODGE_RATING:
                case ITEM_MOD_PARRY_RATING:
                case ITEM_MOD_BLOCK_RATING:
                case ITEM_MOD_BLOCK_VALUE:
                    return weights.defense;
                case ITEM_MOD_HIT_MELEE_RATING:
                case ITEM_MOD_HIT_RANGED_RATING:
                case ITEM_MOD_HIT_SPELL_RATING:
                case ITEM_MOD_HIT_RATING:
                    return weights.hit;
                case ITEM_MOD_CRIT_MELEE_RATING:
                case ITEM_MOD_CRIT_RANGED_RATING:
                case ITEM_MOD_CRIT_SPELL_RATING:
                case ITEM_MOD_CRIT_RATING:
                    return weights.crit;
                case ITEM_MOD_HASTE_MELEE_RATING:
                case ITEM_MOD_HASTE_RANGED_RATING:
                case ITEM_MOD_HASTE_SPELL_RATING:
                case ITEM_MOD_HASTE_RATING:
                    return weights.haste;
                case ITEM_MOD_EXPERTISE_RATING: return weights.expertise;
                case ITEM_MOD_ATTACK_POWER: return weights.attackPower;
                case ITEM_MOD_RANGED_ATTACK_POWER: return weights.rangedAttackPower;
                case ITEM_MOD_MANA_REGENERATION: return weights.manaPerFive;
                case ITEM_MOD_ARMOR_PENETRATION_RATING: return weights.armorPenetration;
                case ITEM_MOD_SPELL_HEALING_DONE:
                case ITEM_MOD_SPELL_DAMAGE_DONE:
                case ITEM_MOD_SPELL_POWER:
                    return weights.spellPower;
                default: return 0.0f;
            }
        }

        uint32_t PreferredArmorSubclass(Player const* bot)
        {
            if (!bot)
                return ITEM_SUBCLASS_ARMOR_CLOTH;

            switch (bot->GetClass())
            {
                case CLASS_DEATH_KNIGHT:
                    return ITEM_SUBCLASS_ARMOR_PLATE;
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                    return bot->GetLevel() >= 40 ? ITEM_SUBCLASS_ARMOR_PLATE : ITEM_SUBCLASS_ARMOR_MAIL;
                case CLASS_HUNTER:
                case CLASS_SHAMAN:
                    return bot->GetLevel() >= 40 ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_LEATHER;
                case CLASS_ROGUE:
                case CLASS_DRUID:
                    return ITEM_SUBCLASS_ARMOR_LEATHER;
                default:
                    return ITEM_SUBCLASS_ARMOR_CLOTH;
            }
        }

        float EquippedScore(Player const* bot, uint8_t slot)
        {
            if (!bot || slot >= EQUIPMENT_SLOT_END)
                return 0.0f;
            Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            return equipped ? EquipmentUtils::ScoreItem(bot, equipped->GetTemplate()) : 0.0f;
        }

        float ReplacedScore(Player const* bot, uint8_t slot, ItemTemplate const* candidate)
        {
            float result = EquippedScore(bot, slot);
            if (slot == EQUIPMENT_SLOT_MAINHAND && IsTwoHanded(candidate))
                result += EquippedScore(bot, EQUIPMENT_SLOT_OFFHAND);
            else if (slot == EQUIPMENT_SLOT_OFFHAND)
            {
                Item* mh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                if (mh && IsTwoHanded(mh->GetTemplate()))
                    result += EquippedScore(bot, EQUIPMENT_SLOT_MAINHAND);
            }
            return result;
        }

        template <typename CanEquip>
        bool FindBestUpgradeImpl(Player* bot, ItemTemplate const* itemTemplate,
            CanEquip&& canEquip, EquipmentUpgrade& upgrade)
        {
            if (!bot || !itemTemplate ||
                (itemTemplate->Class != ITEM_CLASS_WEAPON && itemTemplate->Class != ITEM_CLASS_ARMOR))
                return false;

            float candidateScore = EquipmentUtils::ScoreItem(bot, itemTemplate);
            if (candidateScore <= 0.0f)
                return false;

            bool found = false;
            EquipmentUpgrade best;
            best.candidateScore = candidateScore;
            float bestDelta = -std::numeric_limits<float>::infinity();
            for (uint8_t requestedSlot = EQUIPMENT_SLOT_START; requestedSlot < EQUIPMENT_SLOT_END; ++requestedSlot)
            {
                uint16_t destination = 0;
                if (!canEquip(requestedSlot, destination))
                    continue;

                uint8_t actualSlot = static_cast<uint8_t>(destination & 0xFF);
                if (actualSlot >= EQUIPMENT_SLOT_END)
                    continue;

                float replaced = ReplacedScore(bot, actualSlot, itemTemplate);
                float delta = candidateScore - replaced;
                if (!found || delta > bestDelta)
                {
                    found = true;
                    bestDelta = delta;
                    best.slot = actualSlot;
                    best.replacedScore = replaced;
                }
            }

            if (!found || !best.IsUpgrade())
                return false;
            upgrade = best;
            return true;
        }
    }

    bool EquipmentUpgrade::IsUpgrade() const
    {
        if (candidateScore <= 0.0f)
            return false;
        if (replacedScore <= 0.0f)
            return true;
        return candidateScore >= replacedScore * 1.03f && Delta() >= 0.25f;
    }

    BotSpecialization EquipmentUtils::GetSpecialization(Player const* bot)
    {
        if (!bot)
            return BotSpecialization::Generic;

        switch (bot->GetClass())
        {
            case CLASS_WARRIOR: return BotSpecialization::Arms;
            case CLASS_PALADIN: return BotSpecialization::Retribution;
            case CLASS_HUNTER: return BotSpecialization::Marksmanship;
            case CLASS_ROGUE: return BotSpecialization::Combat;
            case CLASS_PRIEST: return BotSpecialization::Shadow;
            case CLASS_DEATH_KNIGHT: return BotSpecialization::Blood;
            case CLASS_SHAMAN: return BotSpecialization::Enhancement;
            case CLASS_MAGE: return BotSpecialization::Frost;
            case CLASS_WARLOCK: return BotSpecialization::Affliction;
            case CLASS_DRUID: return BotSpecialization::Feral;
            default: return BotSpecialization::Generic;
        }
    }

    const char* EquipmentUtils::GetSpecializationName(BotSpecialization specialization)
    {
        switch (specialization)
        {
            case BotSpecialization::Arms: return "Arms";
            case BotSpecialization::Retribution: return "Retribution";
            case BotSpecialization::Marksmanship: return "Marksmanship";
            case BotSpecialization::Combat: return "Combat";
            case BotSpecialization::Shadow: return "Shadow";
            case BotSpecialization::Blood: return "Blood";
            case BotSpecialization::Enhancement: return "Enhancement";
            case BotSpecialization::Frost: return "Frost";
            case BotSpecialization::Affliction: return "Affliction";
            case BotSpecialization::Feral: return "Feral";
            default: return "Generic";
        }
    }

    float EquipmentUtils::ScoreItem(Player const* bot, ItemTemplate const* itemTemplate)
    {
        if (!bot || !itemTemplate ||
            (itemTemplate->Class != ITEM_CLASS_WEAPON && itemTemplate->Class != ITEM_CLASS_ARMOR))
            return 0.0f;

        StatWeights weights = WeightsFor(GetSpecialization(bot));
        float score = 1.0f + static_cast<float>(itemTemplate->ItemLevel) * 0.05f +
            static_cast<float>(itemTemplate->Quality) * 0.2f;

        for (uint32_t index = 0; index < std::min<uint32_t>(itemTemplate->StatsCount, MAX_ITEM_PROTO_STATS); ++index)
        {
            auto const& stat = itemTemplate->ItemStat[index];
            score += static_cast<float>(stat.ItemStatValue) * RatingWeight(weights, stat.ItemStatType);
        }

        score += static_cast<float>(itemTemplate->Armor) * 0.008f;
        float dps = WeaponDps(itemTemplate);
        score += dps * (IsRangedWeapon(itemTemplate) ? weights.rangedDps : weights.weaponDps);

        // Scaling equipment has no fixed stats in the template. Preserve it
        // as a meaningful candidate instead of reducing it to quality alone.
        if (itemTemplate->ScalingStatDistribution != 0)
            score += static_cast<float>(std::max<uint32_t>(bot->GetLevel(), itemTemplate->ItemLevel)) * 1.5f;

        if (itemTemplate->Class == ITEM_CLASS_ARMOR &&
            itemTemplate->SubClass >= ITEM_SUBCLASS_ARMOR_CLOTH &&
            itemTemplate->SubClass <= ITEM_SUBCLASS_ARMOR_PLATE &&
            itemTemplate->SubClass < PreferredArmorSubclass(bot))
        {
            score *= 0.82f;
        }

        return std::max(score, 0.0f);
    }

    bool EquipmentUtils::FindBestUpgrade(Player* bot, Item* item, EquipmentUpgrade& upgrade)
    {
        ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
        return FindBestUpgradeImpl(bot, itemTemplate,
            [bot, item](uint8_t slot, uint16_t& destination) {
                return bot->CanEquipItem(slot, destination, item, false) == EQUIP_ERR_OK;
            }, upgrade);
    }

    bool EquipmentUtils::FindBestUpgrade(Player* bot, uint32_t itemId, EquipmentUpgrade& upgrade)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        return FindBestUpgradeImpl(bot, itemTemplate,
            [bot, itemId](uint8_t slot, uint16_t& destination) {
                return bot->CanEquipNewItem(slot, destination, itemId, false) == EQUIP_ERR_OK;
            }, upgrade);
    }

    float EquipmentUtils::ScoreQuestReward(Player* bot, uint32_t itemId)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        if (!bot || !itemTemplate)
            return 0.0f;

        EquipmentUpgrade upgrade;
        if (FindBestUpgrade(bot, itemId, upgrade))
            return 1000000.0f + upgrade.Delta();

        if ((itemTemplate->Class == ITEM_CLASS_WEAPON || itemTemplate->Class == ITEM_CLASS_ARMOR) &&
            bot->CanUseItem(itemTemplate) == EQUIP_ERR_OK)
            return 10000.0f + ScoreItem(bot, itemTemplate);

        if (itemTemplate->Class == ITEM_CLASS_CONSUMABLE &&
            bot->CanUseItem(itemTemplate) == EQUIP_ERR_OK)
            return 1000.0f + static_cast<float>(itemTemplate->ItemLevel);

        return static_cast<float>(itemTemplate->SellPrice);
    }
}
