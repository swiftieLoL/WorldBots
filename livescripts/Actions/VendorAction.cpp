#include "VendorAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Helper/InventoryUtils.h"
#include "Helper/EquipmentUtils.h"
#include "Helper/ProgressionUtils.h"
#include "Helper/TimeUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/Constants.h"
#include "Entities/Creature/Creature.h"
#include "Entities/Item/Item.h"
#include "Bag.h"
#include "ItemTemplate.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Entities/Creature/CreatureData.h"
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Actions
{
    bool VendorAction::IsNpcSuppressed(uint32_t entry) const
    {
        auto it = _suppressedNpcEntries.find(entry);
        return it != _suppressedNpcEntries.end() && Helper::MonotonicSeconds() < it->second;
    }

    FailureCategory VendorAction::GetFailureCategory() const
    {
        if (_outcome == ActionOutcome::Running || _outcome == ActionOutcome::Succeeded)
            return FailureCategory::None;
        if (_inventoryCapacityFailure)
            return FailureCategory::InventoryCapacity;
        if (_outcomeReason == "cached vendor destination has no safe navmesh path" ||
            _outcomeReason == "no eligible vendor destination was available" ||
            _outcomeReason == "nearest eligible vendor exceeds the temporary safe-travel radius")
            return FailureCategory::Navigation;
        if (_requireRestock)
            return FailureCategory::ServiceCapability;
        return _relatedNpcEntry != 0 ? FailureCategory::Interaction : FailureCategory::Navigation;
    }

    RecoveryDirective VendorAction::GetRecoveryDirective() const
    {
        return _inventoryCapacityFailure ? RecoveryDirective::Replan : RecoveryDirective::RetryLater;
    }

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
        ResetOutcome();
        _inventoryCapacityFailure = false;
        _relatedNpcEntry = 0;
        _travelLogCooldownMs = 0;
        _hasCachedTarget = false;
        _unreachableVendorCount = 0;
        if (!bot || !bot->IsInWorld())
        {
            Finish(ActionOutcome::RetryableFailure, "bot was not available when vendoring started");
            return;
        }

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [VendorAction] START: Bot '{}' (GUID: {}) entering VendorAction at ({:.1f}, {:.1f}, {:.1f}) Map {} (FreeBagSlots: {})",
                bot->GetName(), bot->GetGUID().GetCounter(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
                Helper::InventoryUtils::CountFreeBagSlots(bot));
        }
    }

    void VendorAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || _completed) return;

        _travelLogCooldownMs = deltaMs >= _travelLogCooldownMs
            ? 0 : _travelLogCooldownMs - deltaMs;

        bool isMoving = movement && movement->HasPath();
        constexpr uint32_t TravelHardTimeoutMs = 10 * 60 * 1000;
        if (_failsafe.CheckMovementProgress(deltaMs, Constants::FailsafeMovingTimeoutMs,
            Constants::FailsafeIdleTimeoutMs, TravelHardTimeoutMs, isMoving,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            if (_hasCachedTarget && _relatedNpcEntry != 0)
            {
                Cache::BotCache::SuppressVendorLocation(_relatedNpcEntry,
                    _cachedTargetMapId, _cachedTargetX, _cachedTargetY, _cachedTargetZ);
                // The exact cached spawn was unreachable. Keep other spawns of
                // the same vendor entry eligible for the next plan.
                _relatedNpcEntry = 0;
            }
            if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] FAILSAFE TIMEOUT: Bot '{}' (GUID: {}) made no travel progress for {}ms (Total: {}ms, IsMoving: {}). Returning control to main loop.",
                    bot->GetName(), bot->GetGUID().GetCounter(), _failsafe.elapsedMs,
                    _failsafe.totalElapsedMs, isMoving ? "Yes" : "No");
            }
            // A five-minute restock backoff follows this action. Classify a
            // destination that made no movement progress as blocked so its
            // 15-minute NPC suppression outlives that backoff and the next
            // plan selects a different service location.
            _outcome = ActionOutcome::Blocked;
            _outcomeReason = std::string("vendor travel failed: ") +
                _failsafe.GetMovementFailureReason();
            _completed = true;
            return;
        }

        // Search for nearest Vendor or Repair NPC within 30 yards
        bool reqRepair = _requireRepair || (!_partyVisit && blackboard.inv.needsRepair);
        if (_partyVisit && _partyMemberGuid)
        {
            if (Player* member = ObjectAccessor::FindPlayer(_partyMemberGuid))
                reqRepair = CalculateRepairCost(member, 1.0f) > 0;
        }
        bool reqVendor = _requireInventoryProgress || _requireRestock || !reqRepair;
        auto supplyCapability = [this](Creature* creature) {
            return creature && !IsNpcSuppressed(creature->GetEntry());
        };
        Creature* nearestVendor = Helper::NpcUtils::FindNearbyServiceNpc(
            bot, reqVendor, reqRepair, Constants::TacticalScanRadius, supplyCapability);

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
                reqVendor, reqRepair, vendorEntry, pos,
                [this](uint32_t entry) {
                    return !IsNpcSuppressed(entry);
                }))
            {
                hasTarget = true;
                _relatedNpcEntry = vendorEntry;
                _hasCachedTarget = true;
                _cachedTargetMapId = pos.mapId;
                _cachedTargetX = pos.x;
                _cachedTargetY = pos.y;
                _cachedTargetZ = pos.z;
                targetX = pos.x;
                targetY = pos.y;
                targetZ = pos.z;
            }
            else if (Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] BotCache FindNearestVendor returned FALSE for Map {} at ({:.1f}, {:.1f}, {:.1f})!",
                    bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            }

            if (hasTarget)
            {
                float dist = Helper::Distance2D(targetX, targetY, bot->GetPositionX(), bot->GetPositionY());

                if (dist > _maxTravelDistance)
                {
                    _outcome = ActionOutcome::Blocked;
                    _relatedNpcEntry = 0;
                    _outcomeReason = "nearest eligible vendor exceeds the temporary safe-travel radius";
                    _completed = true;
                    return;
                }

                if (dist <= Constants::InteractionRange)
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
                        if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
                        {
                            TC_LOG_WARN("server", "[WorldBots] [VendorAction] Bot '{}' reached cached Vendor Entry {} but no live vendor was present; refusing a fallback transaction",
                                bot->GetName(), vendorEntry);
                        }
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

                    Cache::BotCache::ConfirmVendorLocation(vendorCreature->GetEntry(),
                        vendorCreature->GetMapId(), vendorCreature->GetPositionX(),
                        vendorCreature->GetPositionY(), vendorCreature->GetPositionZ());

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

                if (_travelLogCooldownMs == 0 && Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [VendorAction] Moving to distant Vendor Entry {} at ({:.1f}, {:.1f}, {:.1f}) [Distance: {:.1f}yd]",
                        vendorEntry, targetX, targetY, targetZ, dist);
                    _travelLogCooldownMs = 5000;
                }

                if (movement)
                {
                    while (!movement->MoveTo(targetX, targetY, targetZ,
                        BotMovementState::Moving, false))
                    {
                        Cache::BotCache::SuppressVendorLocation(vendorEntry, pos.mapId,
                            targetX, targetY, targetZ);
                        ++_unreachableVendorCount;
                        if (Diagnostics::BotTrace::ShouldLog(bot))
                        {
                            TC_LOG_WARN("server", "[WorldBots] [VendorAction] Bot '{}' failed navmesh path to Vendor Entry {} at ({:.1f}, {:.1f}, {:.1f}); retry candidate {}/{}",
                                bot->GetName(), vendorEntry, targetX, targetY, targetZ,
                                _unreachableVendorCount, Helper::VendorSelectionPolicy::MaxVendorCandidateRetries);
                        }

                        if (_unreachableVendorCount >= Helper::VendorSelectionPolicy::MaxVendorCandidateRetries)
                        {
                            _relatedNpcEntry = 0;
                            SetFailure(ActionOutcome::RetryableFailure,
                                "cached vendor destination has no safe navmesh path",
                                FailureCategory::Navigation,
                                RecoveryDirective::RetryLater);
                            return;
                        }

                        if (!Cache::BotCache::FindNearestVendor(bot->GetMapId(),
                            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                            reqVendor, reqRepair, vendorEntry, pos,
                            [this](uint32_t entry) { return !IsNpcSuppressed(entry); }))
                        {
                            _relatedNpcEntry = 0;
                            SetFailure(ActionOutcome::RetryableFailure,
                                "no eligible vendor destination was available",
                                FailureCategory::Navigation,
                                RecoveryDirective::RetryLater);
                            return;
                        }

                        _relatedNpcEntry = vendorEntry;
                        _hasCachedTarget = true;
                        _cachedTargetMapId = pos.mapId;
                        _cachedTargetX = pos.x;
                        _cachedTargetY = pos.y;
                        _cachedTargetZ = pos.z;
                        targetX = pos.x;
                        targetY = pos.y;
                        targetZ = pos.z;

                        dist = Helper::Distance2D(targetX, targetY, bot->GetPositionX(), bot->GetPositionY());
                        if (dist > _maxTravelDistance)
                        {
                            _relatedNpcEntry = 0;
                            SetFailure(ActionOutcome::Blocked,
                                "nearest eligible vendor exceeds the temporary safe-travel radius",
                                FailureCategory::Navigation,
                                RecoveryDirective::RetryLater);
                            return;
                        }
                    }
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
        _hasCachedTarget = false;
        _relatedNpcEntry = nearestVendor->GetEntry();
        Cache::BotCache::ConfirmVendorLocation(nearestVendor->GetEntry(), nearestVendor->GetMapId(),
            nearestVendor->GetPositionX(), nearestVendor->GetPositionY(), nearestVendor->GetPositionZ());
        if (dist > Constants::InteractionRange)
        {
            if (_travelLogCooldownMs == 0 && Diagnostics::BotTrace::ShouldLog(bot))
            {
                TC_LOG_INFO("server", "[WorldBots] [VendorAction] Approaching in-range Vendor '{}' (Entry: {}, GUID: {}) at ({:.1f}, {:.1f}, {:.1f}) [Distance: {:.1f}yd]",
                    nearestVendor->GetName(), nearestVendor->GetEntry(), nearestVendor->GetGUID().GetCounter(),
                    nearestVendor->GetPositionX(), nearestVendor->GetPositionY(), nearestVendor->GetPositionZ(), dist);
                _travelLogCooldownMs = 5000;
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

        // A party visit escorts the requesting member to a service vendor.
        // The leader is not the subject of that request, so do not mutate or
        // require progress in the leader's inventory during this visit.
        if (_partyVisit)
        {
            _outcome = ActionOutcome::Succeeded;
            _completed = true;
            return;
        }

        // Equip first so an upgrade does not get treated as surplus equipment.
        AutoEquipUpgrades(bot);
        bool restockSatisfied = !_requireRestock;
        if (vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
        {
            SellItems(bot, vendor, totalBagSlots);
            // Reward-space creation has a hard capacity contract. Do not use
            // the slots it just created for new supply stacks before the
            // quest reward is collected.
            if (_requireRestock && _targetFreeSlots == 0)
                restockSatisfied = RestockSupplies(bot, vendor);
        }
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
        else if (_requireRestock && !restockSatisfied)
        {
            _outcome = ActionOutcome::RetryableFailure;
            // Provisioning is independent of this merchant's authored stock.
            // A remaining deficit is a bot/inventory problem, not a reason to
            // blacklist this NPC and travel to a farther merchant.
            _relatedNpcEntry = 0;
            _outcomeReason = "vendor provisioning left a critical supply deficit; retry is deferred";
        }
        else if (_requireInventoryProgress && freeSlotsAfter <= freeSlotsBefore)
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

            Helper::EquipmentUpgrade upgrade;
            if (Helper::EquipmentUtils::FindBestUpgrade(bot, item, upgrade))
            {
                std::string itemName = proto->Name1;
                WorldSession* session = bot->GetSession();
                if (!session) return false;
                WorldPacket packet(CMSG_AUTOEQUIP_ITEM_SLOT, 8 + 1);
                packet << item->GetGUID() << uint8(upgrade.slot);
                session->HandleAutoEquipItemSlotOpcode(packet);
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Vendor] Bot '{}' auto-equipped {} upgrade '{}' in slot {} (score {:.2f} -> {:.2f})",
                        bot->GetName(), Helper::EquipmentUtils::GetSpecializationName(
                            Helper::EquipmentUtils::GetSpecialization(bot)), itemName, upgrade.slot,
                        upgrade.replacedScore, upgrade.candidateScore);
                }
                if (proto->Class == ITEM_CLASS_WEAPON)
                    bot->UpdateWeaponsSkillsToMaxSkillsForLevel();
                return true;
            }
            return false;
        };

        Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item*) {
            processItem(bag, slot);
            return true;
        });
    }

    bool VendorAction::RestockSupplies(Player* bot, Creature* vendor)
    {
        if (!bot || !vendor || !vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
            return false;

        Helper::SupplyProvisionResult provision =
            Helper::ProgressionUtils::ProvisionCriticalSupplies(bot);
        if (provision.MadeProgress() || Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [Vendor] Bot '{}' provisioned {} critical supply item(s) at '{}' (Entry {}): before [{}], after [{}], storage blocked: {}.",
                bot->GetName(), provision.itemsCreated, vendor->GetName(), vendor->GetEntry(),
                Helper::ProgressionUtils::DescribeCriticalSupplyDeficits(provision.before),
                Helper::ProgressionUtils::DescribeCriticalSupplyDeficits(provision.after),
                provision.storageBlocked ? "yes" : "no");
        }
        return provision.Complete();
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
        uint32 currentFreeSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);
        if (cleanupTargetSlots > 0 && currentFreeSlots < cleanupTargetSlots)
        {
            auto discardSlot = [&](uint8 bag, uint8 slot) {
                if (currentFreeSlots >= cleanupTargetSlots) return;

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
                currentFreeSlots++;
            };

            Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item*) {
                discardSlot(bag, slot);
                return currentFreeSlots < cleanupTargetSlots;
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
