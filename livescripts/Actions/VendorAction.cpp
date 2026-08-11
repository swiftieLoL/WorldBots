#include "VendorAction.h"
#include "Helper/InventoryUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Entities/Player/Player.h"
#include "Entities/Creature/Creature.h"
#include "Entities/Item/Item.h"
#include "Bag.h"
#include "ItemTemplate.h"
#include "Server/WorldSession.h"
#include "Opcodes.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Log.h"
#include "Helper/NpcFinder.h"
#include "Cache/BotCache.h"
#include "Diagnostics/BotTrace.h"
#include <cstdio>
#include <limits>
#include <vector>

namespace Actions
{
    namespace
    {
        uint32 CalculateRepairCost(Player* bot, float discountMod)
        {
            if (!bot)
                return 0;

            uint64 totalCost = 0;
            auto addItemCost = [&](Item* item) {
                if (!item)
                    return;

                totalCost += item->CalculateDurabilityRepairCost(discountMod);
            };

            // Mirror Player::DurabilityRepairAll: equipped items, backpack
            // items, equipped bag containers, and the contents of those bags.
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                addItemCost(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

            for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            {
                for (uint8 slot = 0; slot < MAX_BAG_SIZE; ++slot)
                    addItemCost(bot->GetItemByPos(bag, slot));
            }

            return static_cast<uint32>(std::min<uint64>(totalCost, std::numeric_limits<uint32>::max()));
        }
    }

    void VendorAction::Start(Player* bot, MovementManager* movement)
    {
        _failsafe.Reset();
        _started = false;
        _completed = false;
        _inventoryCapacityFailure = false;
        _outcome = ActionOutcome::Running;
        _relatedNpcEntry = 0;
        _outcomeReason.clear();
        if (!bot || !bot->IsInWorld())
        {
            _outcome = ActionOutcome::RetryableFailure;
            _outcomeReason = "bot was not available when vendoring started";
            _completed = true;
            return;
        }

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [VendorAction] START: Bot '{}' (GUID: {}) entering VendorAction at ({:.1f}, {:.1f}, {:.1f}) Map {} (FreeBagSlots: {})",
                bot->GetName(), bot->GetGUID().GetCounter(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
                Helper::InventoryUtils::CountFreeBagSlots(bot));
        }

        _started = true;
    }

    void VendorAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || _completed) return;

        bool isMoving = movement && movement->HasPath();
        constexpr uint32_t TravelHardTimeoutMs = 10 * 60 * 1000;
        if (_failsafe.CheckMovementProgress(deltaMs, Constants::FailsafeMovingTimeoutMs,
            Constants::FailsafeIdleTimeoutMs, TravelHardTimeoutMs, isMoving,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] FAILSAFE TIMEOUT: Bot '{}' (GUID: {}) made no travel progress for {}ms (Total: {}ms, IsMoving: {}). Returning control to main loop.",
                    bot->GetName(), bot->GetGUID().GetCounter(), _failsafe.elapsedMs,
                    _failsafe.totalElapsedMs, isMoving ? "Yes" : "No");
            }
            _outcome = ActionOutcome::RetryableFailure;
            _outcomeReason = "vendor travel made no progress before the failsafe timeout";
            _completed = true;
            return;
        }

        // Search for nearest Vendor or Repair NPC within 30 yards
        bool reqRepair = _requireRepair || blackboard.inv.needsRepair;
        bool reqVendor = _requireInventoryProgress || !reqRepair;
        Creature* nearestVendor = Helper::NpcUtils::FindNearbyServiceNpc(bot, reqVendor, reqRepair, 30.0f);

        if (!nearestVendor)
        {
            float targetX = 0.0f;
            float targetY = 0.0f;
            float targetZ = 0.0f;
            uint32_t vendorEntry = 0;
            bool hasTarget = false;

            // Resolve against the exact services required by this run. The
            // blackboard's general-purpose vendor may be repair-only or
            // vendor-only and is therefore not safe for a combined visit.
            Cache::PositionInfo pos;
            if (Cache::BotCache::FindNearestVendor(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                reqVendor, reqRepair, vendorEntry, pos))
            {
                hasTarget = true;
                targetX = pos.x;
                targetY = pos.y;
                targetZ = pos.z;
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [VendorAction] BotCache Found Service NPC Entry {} at ({:.1f}, {:.1f}, {:.1f}) Map {}",
                        vendorEntry, targetX, targetY, targetZ, pos.mapId);
                }
            }
            else if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] BotCache FindNearestVendor returned FALSE for Map {} at ({:.1f}, {:.1f}, {:.1f})!",
                    bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            }

            if (hasTarget)
            {
                float dist = Helper::Distance2D(targetX, targetY, bot->GetPositionX(), bot->GetPositionY());

                if (dist <= 4.5f)
                {
                    if (movement)
                    {
                        movement->Stop();
                    }

                    Creature* vendorCreature = Helper::NpcUtils::FindNearbyCreatureByEntry(bot, vendorEntry, 15.0f);
                    bool validVendor = vendorCreature && vendorCreature->IsAlive() &&
                        (!reqVendor || vendorCreature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR)) &&
                        (!reqRepair || vendorCreature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR));
                    if (!validVendor)
                    {
                        TC_LOG_WARN("server", "[WorldBots] [VendorAction] Bot '{}' reached cached Vendor Entry {} but no live vendor was present; refusing a fallback transaction",
                            bot->GetName(), vendorEntry);
                        Cache::BotCache::SuppressVendorLocation(vendorEntry, pos.mapId, targetX, targetY, targetZ);
                        _outcome = ActionOutcome::RetryableFailure;
                        // The location is stale, not necessarily every spawn
                        // of this NPC entry. Cache suppression lets the next
                        // plan consider another stationary spawn immediately.
                        _relatedNpcEntry = 0;
                        _outcomeReason = "cached vendor had no interactable live creature at its destination";
                        _completed = true;
                        return;
                    }

                    Helper::InteractionStatus status = Helper::NpcUtils::GetInteractionStatus(
                        bot, vendorCreature, Constants::VendorInteractionRange);
                    if (status == Helper::InteractionStatus::NeedsMovement)
                    {
                        if (movement)
                        {
                            movement->MoveTo(vendorCreature->GetPositionX(), vendorCreature->GetPositionY(),
                                vendorCreature->GetPositionZ(), BotMovementState::Moving, false);
                        }
                        else
                        {
                            _outcome = ActionOutcome::RetryableFailure;
                            _relatedNpcEntry = vendorEntry;
                            _outcomeReason = "vendor required movement but no movement manager was available";
                            _completed = true;
                        }
                        return;
                    }
                    if (status != Helper::InteractionStatus::Ready)
                    {
                        _outcome = ActionOutcome::RetryableFailure;
                        _relatedNpcEntry = vendorEntry;
                        _outcomeReason = "live vendor became invalid before interaction";
                        _completed = true;
                        return;
                    }

                    if (Diagnostics::BotTrace::ShouldLog(bot))
                    {
                        TC_LOG_INFO("server", "[WorldBots] [VendorAction] ARRIVED AT VENDOR POSITION ({:.1f}yd): Bot '{}' executing transaction at Vendor Entry {} (Creature: '{}')",
                            dist, bot->GetName(), vendorEntry, vendorCreature->GetName());
                    }

                    Helper::NpcUtils::PrepareCreatureInteraction(bot, vendorCreature);

                    ExecuteTransaction(bot, vendorCreature, blackboard.inv.totalBagSlots, true);
                    return;
                }

                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [VendorAction] Moving to distant Vendor Entry {} at ({:.1f}, {:.1f}, {:.1f}) [Distance: {:.1f}yd]",
                        vendorEntry, targetX, targetY, targetZ, dist);
                }

                if (movement)
                {
                    movement->MoveTo(targetX, targetY, targetZ, BotMovementState::Moving, false);
                }
                return;
            }

            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] NO VENDOR FOUND for Bot '{}' in 30yd spatial scan or BotCache! FreeBagSlots: {}/{}. Completing VendorAction.",
                    bot->GetName(), blackboard.inv.freeBagSlots, blackboard.inv.totalBagSlots);
            }

            _outcome = ActionOutcome::RetryableFailure;
            _outcomeReason = "no eligible vendor destination was available";
            _completed = true;
            return;
        }

        float dist = bot->GetDistance(nearestVendor);
        if (dist > 4.5f)
        {
            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] Approaching in-range Vendor '{}' (Entry: {}, GUID: {}) at ({:.1f}, {:.1f}, {:.1f}) [Distance: {:.1f}yd]",
                    nearestVendor->GetName(), nearestVendor->GetEntry(), nearestVendor->GetGUID().GetCounter(),
                    nearestVendor->GetPositionX(), nearestVendor->GetPositionY(), nearestVendor->GetPositionZ(), dist);
            }

            if (movement)
            {
                movement->MoveTo(nearestVendor->GetPositionX(), nearestVendor->GetPositionY(), nearestVendor->GetPositionZ(), BotMovementState::Moving, false);
            }
            return;
        }

        // Within 4.5 yards: Interact with Vendor
        Helper::InteractionStatus interactionStatus = Helper::NpcUtils::GetInteractionStatus(
            bot, nearestVendor, Constants::VendorInteractionRange);
        if (interactionStatus == Helper::InteractionStatus::NeedsMovement)
        {
            if (movement)
                movement->MoveTo(nearestVendor->GetPositionX(), nearestVendor->GetPositionY(),
                    nearestVendor->GetPositionZ(), BotMovementState::Moving, false);
            return;
        }
        if (interactionStatus != Helper::InteractionStatus::Ready)
        {
            _outcome = ActionOutcome::RetryableFailure;
            _relatedNpcEntry = nearestVendor->GetEntry();
            _outcomeReason = "nearby vendor became invalid before interaction";
            _completed = true;
            return;
        }

        if (movement)
        {
            movement->Stop();
        }

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [VendorAction] INTERACTION EXECUTED: Bot '{}' interacting with Vendor '{}' (Entry: {}) within {:.1f}yd.",
                bot->GetName(), nearestVendor->GetName(), nearestVendor->GetEntry(), dist);
        }

        Helper::NpcUtils::PrepareCreatureInteraction(bot, nearestVendor);

        ExecuteTransaction(bot, nearestVendor, blackboard.inv.totalBagSlots, false);
    }

    void VendorAction::Stop(Player* bot, MovementManager* movement)
    {
        if (movement)
        {
            movement->Stop();
        }
    }

    void VendorAction::ExecuteTransaction(Player* bot, Creature* vendor, uint32_t totalBagSlots, bool logInventory)
    {
        uint32 freeSlotsBefore = Helper::InventoryUtils::CountFreeBagSlots(bot);

        // Equip first so an upgrade does not get treated as surplus equipment.
        AutoEquipUpgrades(bot);
        if (vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
            SellItems(bot, vendor, totalBagSlots);
        RepairAll(bot, vendor);

        if (logInventory && Diagnostics::BotTrace::ShouldLog(bot))
        {
            Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item* item) {
                if (!item) return true;
                ItemTemplate const* itemTemplate = item->GetTemplate();
                const char* name = itemTemplate ? itemTemplate->Name1.c_str() : "<unknown>";
                TC_LOG_INFO("server", "[WorldBots] [Vendor] Inventory: Bag {} Slot {} -> '{}' (Entry {}) x{}",
                    bag, slot, name, item->GetEntry(), item->GetCount());
                return true;
            });
        }

        uint32 freeSlotsAfter = Helper::InventoryUtils::CountFreeBagSlots(bot);
        float discountMod = bot->GetReputationPriceDiscount(vendor);
        uint32 remainingRepairCost = CalculateRepairCost(bot, discountMod);

        if (_targetFreeSlots > 0 && freeSlotsAfter < _targetFreeSlots)
        {
            _outcome = ActionOutcome::Blocked;
            // The vendor completed every transaction it could. The remaining
            // items are protected by inventory policy, so suppress the
            // capacity attempt rather than falsely blacklisting this NPC.
            _relatedNpcEntry = 0;
            _inventoryCapacityFailure = true;
            _outcomeReason = "vendor cleanup could not reserve enough quest reward space";
        }
        else if (_requireRepair && remainingRepairCost > 0)
        {
            _outcome = ActionOutcome::RetryableFailure;
            _relatedNpcEntry = vendor->GetEntry();
            _outcomeReason = "paid repair left damaged equipment unrepaired";
        }
        else if (_requireInventoryProgress && freeSlotsBefore == 0 && freeSlotsAfter == 0)
        {
            _outcome = ActionOutcome::Blocked;
            _relatedNpcEntry = 0;
            _inventoryCapacityFailure = true;
            _outcomeReason = "vendor transaction could not free any inventory capacity";
        }
        else
        {
            _outcome = ActionOutcome::Succeeded;
        }
        _completed = true;
    }

    void VendorAction::AutoEquipUpgrades(Player* bot)
    {
        if (!bot) return;

        auto processItem = [&](uint8 bag, uint8 slot) -> bool {
            Item* item = bot->GetItemByPos(bag, slot);
            if (!item) return false;

            ItemTemplate const* proto = item->GetTemplate();
            if (!proto) return false;
            if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR) return false;
            if (bot->CanUseItem(proto) != EQUIP_ERR_OK) return false;

            bool isUpgrade = false;
            for (uint8 equipSlot = EQUIPMENT_SLOT_START; equipSlot < EQUIPMENT_SLOT_END; ++equipSlot)
            {
                uint16 dest = 0;
                if (bot->CanEquipItem(equipSlot, dest, item, true) == EQUIP_ERR_OK)
                {
                    Item* equippedItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, equipSlot);
                    if (!equippedItem)
                    {
                        isUpgrade = true;
                        break;
                    }
                    else
                    {
                        ItemTemplate const* equippedProto = equippedItem->GetTemplate();
                        if (equippedProto && proto->ItemLevel > equippedProto->ItemLevel)
                        {
                            isUpgrade = true;
                            break;
                        }
                    }
                }
            }

            if (isUpgrade)
            {
                std::string itemName = proto->Name1;
                WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
                packet << uint8(bag) << uint8(slot);
                bot->GetSession()->HandleAutoEquipItemOpcode(packet);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Vendor] Bot '{}' auto-equipped item upgrade '{}'",
                        bot->GetName(), itemName);
                }
                return true;
            }
            return false;
        };

        Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item*) {
            processItem(bag, slot);
            return true;
        });
    }

    void VendorAction::SellItems(Player* bot, Creature* vendor, uint32_t totalBagSlots)
    {
        if (!bot || !vendor) return;
        uint32 freeSlotsBefore = Helper::InventoryUtils::CountFreeBagSlots(bot);
        bool lowSpace = freeSlotsBefore <= 3;
        Helper::InventoryPolicyContext inventoryPolicy =
            Helper::InventoryUtils::BuildPolicyContext(bot, lowSpace);
        uint32 itemsSold = 0;
        uint32 itemsDiscarded = 0;

        auto sellSlot = [&](uint8 bag, uint8 slot) {
            Item* item = bot->GetItemByPos(bag, slot);
            if (!item) return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!Helper::InventoryUtils::ClassifyForSpace(bot, item, inventoryPolicy).sell)
                return;

            uint32 sellGain = proto->SellPrice * item->GetCount();
            bot->ModifyMoney(sellGain);
            bot->DestroyItem(bag, slot, true);
            itemsSold++;
        };

        Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item*) {
            sellSlot(bag, slot);
            return true;
        });

        // If selling safe junk did not free enough space, only discard poor
        // unsellable junk. Valuable, consumable, equipment, and ordinary
        // unsellable items are never destroyed merely to make reward space.
        uint32 cleanupTargetSlots = _targetFreeSlots > 0 ? _targetFreeSlots :
            (_requireInventoryProgress && freeSlotsBefore == 0 ? 1u : 0u);
        if (cleanupTargetSlots > 0 &&
            Helper::InventoryUtils::CountFreeBagSlots(bot) < cleanupTargetSlots)
        {
            auto discardSlot = [&](uint8 bag, uint8 slot) {
                if (Helper::InventoryUtils::CountFreeBagSlots(bot) >= cleanupTargetSlots) return;

                Item* item = bot->GetItemByPos(bag, slot);
                if (!item) return;

                ItemTemplate const* proto = item->GetTemplate();
                Helper::InventoryItemDecision decision =
                    Helper::InventoryUtils::ClassifyForSpace(bot, item, inventoryPolicy);
                if (!decision.discardWhenFull) return;

                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Vendor] Bot '{}' discarded unsellable item '{}' (Entry: {}) to reserve quest reward space",
                        bot->GetName(), proto->Name1.c_str(), proto->ItemId);
                }
                bot->DestroyItem(bag, slot, true);
                itemsDiscarded++;
            };

            Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item*) {
                discardSlot(bag, slot);
                return Helper::InventoryUtils::CountFreeBagSlots(bot) < cleanupTargetSlots;
            });
        }

        uint32 freeSlotsAfter = Helper::InventoryUtils::CountFreeBagSlots(bot);
        if (Diagnostics::BotTrace::ShouldLog(bot) &&
            (itemsSold > 0 || itemsDiscarded > 0 || freeSlotsBefore == 0))
        {
            TC_LOG_INFO("server", "[WorldBots] [Vendor] Bot '{}' inventory cleanup at '{}' (Entry: {}): sold {} sellable item stacks, discarded {} unsellable item stacks, free slots {}/{} -> {}/{}",
                bot->GetName(), vendor ? vendor->GetName() : "Fallback Vendor", vendor ? vendor->GetEntry() : 0,
                itemsSold, itemsDiscarded, freeSlotsBefore, totalBagSlots,
                freeSlotsAfter, totalBagSlots);
        }
    }

    void VendorAction::RepairAll(Player* bot, Creature* vendor)
    {
        if (!bot || !vendor) return;
        if (!vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR)) return;

        uint32 repairCost = CalculateRepairCost(bot, 1.0f);
        if (repairCost == 0)
            return;

        // Repairs are an intentional economy exception for autonomous bots.
        // Requiring payment traps newly created bots with damaged gear and no
        // copper in a repair/failure/blacklist loop. Keep the armorer visit so
        // repair behavior remains spatially believable, but repair directly
        // without charging money or involving a guild bank.
        bot->DurabilityRepairAll(false, 1.0f, false);

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Vendor] Bot '{}' repaired all gear for free at vendor '{}' (Normal cost: {} copper).",
                bot->GetName(), vendor->GetName(), repairCost);
        }
    }
}
