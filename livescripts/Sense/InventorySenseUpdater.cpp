#include "SenseUpdaters.h"
#include "Globals/ObjectMgr.h"
#include "Helper/InventoryUtils.h"
#include "Helper/ProgressionUtils.h"
#include "Cache/BotCache.h"
#include "Player.h"
#include "Bag.h"

namespace Sense
{
    bool InventorySenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        return Detail::ServiceSubstate(bb.inv, deltaMs,
            [&]() { Refresh(bot, bb.inv); });
    }

    void InventorySenseUpdater::Refresh(Player* bot, Blackboard::InventoryState& inv)
    {
        inv.freeBagSlots = 0;
        inv.totalBagSlots = 0;
        inv.hasItemsToSell = false;
        inv.needsRepair = false;
        inv.bagsFull = false;
        inv.hasHealthPotion = false;
        inv.hasManaPotion = false;
        inv.hasFood = false;
        inv.hasWater = false;
        inv.needsRestock = false;
        inv.nearestVendorEntry = 0;
        inv.vendorPosition = {};

        if (!bot || !bot->IsInWorld()) return;

        inv.freeBagSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);
        Helper::InventoryPolicyContext inventoryPolicy =
            Helper::InventoryUtils::BuildPolicyContext(bot, inv.freeBagSlots <= 3);

        auto inspectItem = [&](Item* item) {
            if (!item) return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto) return;

            if (Helper::InventoryUtils::IsSellableForSpace(bot, item, inventoryPolicy))
                inv.hasItemsToSell = true;

            if (proto->Class == ITEM_CLASS_CONSUMABLE)
            {
                if (proto->SubClass == ITEM_SUBCLASS_POTION)
                {
                    for (const auto& spell : proto->Spells)
                    {
                        if (spell.SpellCategory == 4)
                            inv.hasHealthPotion = true;
                        else if (spell.SpellCategory == 5)
                            inv.hasManaPotion = true;
                    }
                }
                else if (proto->SubClass == ITEM_SUBCLASS_FOOD)
                {
                    Helper::RecoveryConsumableRole role = Helper::InventoryUtils::GetRecoveryConsumableRole(proto);
                    inv.hasFood = inv.hasFood || role == Helper::RecoveryConsumableRole::Food ||
                        role == Helper::RecoveryConsumableRole::FoodAndDrink;
                    inv.hasWater = inv.hasWater || role == Helper::RecoveryConsumableRole::Drink ||
                        role == Helper::RecoveryConsumableRole::FoodAndDrink;
                }
            }
        };

        inv.totalBagSlots = (INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START);

        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Bag* bag = bot->GetBagByPos(i))
            {
                ItemTemplate const* proto = bag->GetTemplate();
                if (proto && proto->BagFamily == 0)
                    inv.totalBagSlots += bag->GetBagSize();
            }
        }

        Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8, uint8, Item* item) -> bool {
            inspectItem(item);
            return true;
        });

        inv.bagsFull = (inv.freeBagSlots == 0);
        inv.lowBagSpace = (inv.freeBagSlots <= 3);
        inv.needsRestock = Helper::ProgressionUtils::NeedsCriticalRestock(bot);

        uint32 totalMaxDur = 0;
        uint32 totalCurDur = 0;
        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        {
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                uint32 maxDur = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
                if (maxDur > 0)
                {
                    uint32 curDur = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
                    totalMaxDur += maxDur;
                    totalCurDur += curDur;
                    if ((float)curDur / (float)maxDur < 0.8f)
                    {
                        inv.needsRepair = true;
                    }
                }
            }
        }
        inv.durabilityPct = (totalMaxDur > 0) ? static_cast<uint8_t>((totalCurDur * 100) / totalMaxDur) : 100;

        if (inv.hasItemsToSell || inv.needsRepair || inv.bagsFull || inv.needsRestock)
        {
            Cache::PositionInfo pos;
            uint32_t entry = 0;
            bool requireVendor = inv.hasItemsToSell || inv.needsRestock || !inv.needsRepair;
            if (Cache::BotCache::FindNearestVendor(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                requireVendor, inv.needsRepair, entry, pos))
            {
                inv.nearestVendorEntry = entry;
                inv.vendorPosition.mapId = pos.mapId;
                inv.vendorPosition.x = pos.x;
                inv.vendorPosition.y = pos.y;
                inv.vendorPosition.z = pos.z;
            }
        }
    }

}

