#include "Globals/ObjectMgr.h"
#include "InventoryUtils.h"

#include "QuestDef.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "Log.h"
#include <algorithm>

namespace Helper
{
    namespace
    {
        constexpr uint32_t HearthstoneItemId = 6948;

        bool IsActiveQuestItem(Player* bot, uint32_t itemId)
        {
            if (!bot || !itemId)
                return false;

            for (uint8 questSlot = 0; questSlot < MAX_QUEST_LOG_SIZE; ++questSlot)
            {
                uint32 questId = bot->GetQuestSlotQuestId(questSlot);
                if (!questId)
                    continue;

                QuestStatus status = bot->GetQuestStatus(questId);
                if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;

                if (quest->GetSrcItemId() == itemId)
                    return true;

                for (uint8 index = 0; index < QUEST_ITEM_OBJECTIVES_COUNT; ++index)
                {
                    if (quest->RequiredItemId[index] == itemId)
                        return true;
                }

                for (uint8 index = 0; index < QUEST_SOURCE_ITEM_IDS_COUNT; ++index)
                {
                    if (quest->ItemDrop[index] == itemId)
                        return true;
                }
            }

            return false;
        }
    }

    bool InventoryUtils::IsMandatoryInventoryItem(Player* bot, ItemTemplate const* proto)
    {
        return !bot || !proto || proto->ItemId == HearthstoneItemId ||
            IsActiveQuestItem(bot, proto->ItemId);
    }

    RecoveryConsumableRole InventoryUtils::GetRecoveryConsumableRole(ItemTemplate const* proto)
    {
        if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE ||
            proto->SubClass != ITEM_SUBCLASS_FOOD)
            return RecoveryConsumableRole::None;

        bool food = false;
        bool drink = false;
        for (auto const& itemSpell : proto->Spells)
        {
            if (itemSpell.SpellId <= 0)
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(itemSpell.SpellId);
            if (!spell)
                continue;

            switch (spell->GetSpellSpecific())
            {
                case SPELL_SPECIFIC_FOOD:
                    food = true;
                    break;
                case SPELL_SPECIFIC_DRINK:
                    drink = true;
                    break;
                case SPELL_SPECIFIC_FOOD_AND_DRINK:
                    food = true;
                    drink = true;
                    break;
                default:
                    break;
            }
        }

        if (food && drink)
            return RecoveryConsumableRole::FoodAndDrink;
        if (food)
            return RecoveryConsumableRole::Food;
        if (drink)
            return RecoveryConsumableRole::Drink;
        return RecoveryConsumableRole::None;
    }

    InventoryPolicyContext InventoryUtils::BuildPolicyContext(Player* bot, bool lowSpace)
    {
        InventoryPolicyContext context;
        context.lowSpace = lowSpace;
        if (!bot || !lowSpace)
            return context;

        std::vector<RecoveryStackCandidate> candidates;
        ForEachBagItem(bot, [&](uint8, uint8, Item* item) {
            ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
            RecoveryConsumableRole role = GetRecoveryConsumableRole(proto);
            if (role == RecoveryConsumableRole::None)
                return true;

            candidates.push_back({
                item->GetPos(),
                proto->ItemLevel,
                item->GetCount(),
                role == RecoveryConsumableRole::Food || role == RecoveryConsumableRole::FoodAndDrink,
                role == RecoveryConsumableRole::Drink || role == RecoveryConsumableRole::FoodAndDrink,
                bot->CanUseItem(proto) == EQUIP_ERR_OK
            });
            return true;
        });

        bool needsDrink = bot->GetMaxPower(POWER_MANA) > 0;
        context.reserves = SelectRecoveryReserves(candidates, needsDrink);
        return context;
    }

    InventoryItemDecision InventoryUtils::ClassifyForSpace(Player* bot, Item* item,
        InventoryPolicyContext const& context)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!bot || !proto)
            return { InventoryItemDisposition::Invalid, false, false, "invalid item" };

        if (proto->ItemId == HearthstoneItemId)
            return { InventoryItemDisposition::ProtectHearthstone, false, false, "protected: Hearthstone" };

        if (IsActiveQuestItem(bot, proto->ItemId))
            return { InventoryItemDisposition::ProtectActiveQuest, false, false, "protected: active quest item" };

        if (proto->SellPrice == 0)
        {
            if (proto->Quality == ITEM_QUALITY_POOR)
                return { InventoryItemDisposition::DiscardPoorNoValue, false, true, "discardable when full: poor item with no vendor value" };
            return { InventoryItemDisposition::ProtectNoSellValue, false, false, "protected: no vendor value" };
        }

        if (proto->Quality == ITEM_QUALITY_POOR)
            return { InventoryItemDisposition::SellPoor, true, false, "sell: poor quality" };

        if (!context.lowSpace)
            return { InventoryItemDisposition::ProtectWhileBagsHealthy, false, false, "protected: bags are not low" };

        bool equipment = proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR;
        if (equipment)
        {
            // AutoEquipUpgrades runs before selling. Remaining low-quality bag
            // equipment is surplus, while rare/epic pieces remain protected.
            if (proto->Quality <= ITEM_QUALITY_UNCOMMON)
                return { InventoryItemDisposition::SellSurplusEquipment, true, false, "sell: surplus low-quality equipment" };
            return { InventoryItemDisposition::ProtectHighQuality, false, false, "protected: rare-or-better equipment" };
        }

        if (proto->Class == ITEM_CLASS_CONSUMABLE)
        {
            RecoveryConsumableRole role = GetRecoveryConsumableRole(proto);
            if (role != RecoveryConsumableRole::None)
            {
                bool retainedFood = item->GetPos() == context.reserves.foodPosition;
                bool retainedDrink = item->GetPos() == context.reserves.drinkPosition;
                if (retainedFood)
                    return { InventoryItemDisposition::ProtectFoodReserve, false, false, "protected: retained food reserve" };
                if (retainedDrink)
                    return { InventoryItemDisposition::ProtectDrinkReserve, false, false, "protected: retained drink reserve" };
                if (role == RecoveryConsumableRole::Drink)
                    return { InventoryItemDisposition::SellSurplusDrink, true, false, "sell: surplus drink stack" };
                return { InventoryItemDisposition::SellSurplusFood, true, false, "sell: surplus food stack" };
            }
            return { InventoryItemDisposition::ProtectConsumable, false, false, "protected: non-recovery consumable" };
        }

        if (proto->Class == ITEM_CLASS_QUEST)
            return { InventoryItemDisposition::ProtectQuestClass, false, false, "protected: quest-class item" };

        if (proto->Quality == ITEM_QUALITY_NORMAL)
            return { InventoryItemDisposition::SellCommon, true, false, "sell: common non-consumable" };

        if (proto->Quality > ITEM_QUALITY_NORMAL)
            return { InventoryItemDisposition::ProtectHighQuality, false, false, "protected: uncommon-or-better non-equipment" };

        return { InventoryItemDisposition::ProtectByPolicy, false, false, "protected by inventory policy" };
    }

    bool InventoryUtils::IsSellableForSpace(Player* bot, Item* item,
        InventoryPolicyContext const& context)
    {
        return ClassifyForSpace(bot, item, context).sell;
    }

    bool InventoryUtils::EnsureStarterBags(Player* bot, uint32_t bagItemId, uint32_t bagCount)
    {
        if (!bot || bagItemId == 0 || bagCount == 0)
            return false;

        ItemTemplate const* bagTemplate = sObjectMgr->GetItemTemplate(bagItemId);
        if (!bagTemplate || bagTemplate->Class != ITEM_CLASS_CONTAINER)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Inventory] Starter bag item {} is missing or is not a container.",
                bagItemId);
            return false;
        }

        uint32_t equipped = 0;
        bool added = false;
        for (uint8 slot = INVENTORY_SLOT_BAG_START;
             slot < INVENTORY_SLOT_BAG_END && equipped < bagCount; ++slot)
        {
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                continue;

            uint16 destination = 0;
            InventoryResult result = bot->CanEquipNewItem(slot, destination, bagItemId, false);
            if (result != EQUIP_ERR_OK || !bot->EquipNewItem(destination, bagItemId, true))
            {
                TC_LOG_ERROR("server", "[WorldBots] [Inventory] Could not equip starter bag item {} in slot {} for bot '{}'; result {}.",
                    bagItemId, static_cast<uint32_t>(slot), bot->GetName(), static_cast<uint32_t>(result));
                continue;
            }

            ++equipped;
            added = true;
        }

        if (added)
        {
            TC_LOG_INFO("server", "[WorldBots] [Inventory] Equipped {} starter bag(s), item {}, for bot '{}'.",
                equipped, bagItemId, bot->GetName());
        }
        return added;
    }
}
