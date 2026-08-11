#pragma once

#include "Player.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Bag.h"
#include "ConsumableReservePolicy.h"
#include <cstdint>

namespace Helper
{
    enum class InventoryItemDisposition : uint8_t
    {
        SellPoor,
        SellCommon,
        SellSurplusEquipment,
        SellSurplusFood,
        SellSurplusDrink,
        DiscardPoorNoValue,
        ProtectHearthstone,
        ProtectActiveQuest,
        ProtectNoSellValue,
        ProtectConsumable,
        ProtectFoodReserve,
        ProtectDrinkReserve,
        ProtectQuestClass,
        ProtectHighQuality,
        ProtectWhileBagsHealthy,
        ProtectByPolicy,
        Invalid
    };

    struct InventoryItemDecision
    {
        InventoryItemDisposition disposition = InventoryItemDisposition::Invalid;
        bool sell = false;
        bool discardWhenFull = false;
        const char* reason = "invalid item";
    };

    enum class RecoveryConsumableRole : uint8_t
    {
        None,
        Food,
        Drink,
        FoodAndDrink
    };

    struct InventoryPolicyContext
    {
        bool lowSpace = false;
        RecoveryReserveSelection reserves;
    };

    class InventoryUtils
    {
    public:
        template<typename VisitorFunc>
        static void ForEachBagItem(Player* bot, VisitorFunc&& visitor)
        {
            if (!bot) return;

            // 1. Backpack (Bag 0)
            for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (item)
                {
                    if (!visitor(INVENTORY_SLOT_BAG_0, slot, item))
                        return;
                }
            }

            // 2. Bags 1-4
            for (uint8_t i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            {
                if (Bag* bag = bot->GetBagByPos(i))
                {
                    for (uint32_t j = 0; j < bag->GetBagSize(); ++j)
                    {
                        if (Item* item = bot->GetItemByPos(i, j))
                        {
                            if (!visitor(i, static_cast<uint8_t>(j), item))
                                return;
                        }
                    }
                }
            }
        }

        static uint32_t CountFreeBagSlots(Player* bot)
        {
            if (!bot) return 0;

            uint32_t freeSlots = 0;
            for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    freeSlots++;
            }

            for (uint8_t i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            {
                if (Bag* bag = bot->GetBagByPos(i))
                    freeSlots += bag->GetFreeSlots();
            }

            return freeSlots;
        }

        // Shared inventory policy used by both sensing and VendorAction. This
        // keeps the brain's "has items to sell" state aligned with what the
        // transaction will actually remove.
        static InventoryPolicyContext BuildPolicyContext(Player* bot, bool lowSpace);
        static RecoveryConsumableRole GetRecoveryConsumableRole(ItemTemplate const* proto);
        static InventoryItemDecision ClassifyForSpace(Player* bot, Item* item,
            InventoryPolicyContext const& context);
        static bool IsMandatoryInventoryItem(Player* bot, ItemTemplate const* proto);
        static bool IsSellableForSpace(Player* bot, Item* item,
            InventoryPolicyContext const& context);

        // Fills empty equipped bag slots without replacing or moving any
        // existing bag. Returns true when at least one bag was added.
        static bool EnsureStarterBags(Player* bot, uint32_t bagItemId, uint32_t bagCount);
    };
}
