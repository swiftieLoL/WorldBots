#include "Globals/ObjectMgr.h"
#include "InventoryUtils.h"
#include "Diagnostics/BotTrace.h"

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
                ProvidesFood(role),
                ProvidesDrink(role),
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
            return { false, false, "invalid item" };

        if (proto->ItemId == HearthstoneItemId)
            return { false, false, "protected: Hearthstone" };

        if (IsActiveQuestItem(bot, proto->ItemId))
            return { false, false, "protected: active quest item" };

        if (proto->Class == ITEM_CLASS_REAGENT ||
            (proto->Class == ITEM_CLASS_MISC && proto->SubClass == ITEM_SUBCLASS_JUNK_REAGENT))
            return { false, false, "protected: spell reagent" };

        if (proto->Class == ITEM_CLASS_PROJECTILE)
            return { false, false, "protected: ammunition" };

        if (proto->SellPrice == 0)
        {
            if (proto->Quality == ITEM_QUALITY_POOR)
                return { false, true, "discardable when full: poor item with no vendor value" };
            if (proto->Class == ITEM_CLASS_QUEST)
                return { false, true, "discardable when full: obsolete quest item with no vendor value" };
            if (proto->Class == ITEM_CLASS_RECIPE)
                return { false, true, "discardable when full: unsellable recipe" };
            return { false, false, "protected: no vendor value" };
        }

        if (proto->Quality == ITEM_QUALITY_POOR)
            return { true, false, "sell: poor quality" };

        if (proto->Class == ITEM_CLASS_QUEST)
            return { true, false, "sell: obsolete quest item" };

        if (!context.lowSpace)
            return { false, false, "protected: bags are not low" };

        // Evaluated when bag space is low (<= 3 free slots)
        if (proto->Class == ITEM_CLASS_RECIPE)
            return { true, false, "sell: recipe" };

        if (proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_GEM)
            return { true, false, "sell: surplus trade goods / gem for space" };

        if (proto->Class == ITEM_CLASS_CONTAINER)
            return { true, false, "sell: surplus unequipped container" };

        bool equipment = proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR;
        if (equipment)
        {
            // AutoEquipUpgrades runs before selling. Remaining bag equipment is surplus.
            if (proto->Quality <= ITEM_QUALITY_RARE ||
                bot->CanUseItem(proto) != EQUIP_ERR_OK ||
                proto->RequiredLevel > bot->GetLevel() + 3)
            {
                return { true, false, "sell: surplus equipment" };
            }
            return { true, false, "sell: surplus epic equipment" };
        }

        if (proto->Class == ITEM_CLASS_CONSUMABLE)
        {
            RecoveryConsumableRole role = GetRecoveryConsumableRole(proto);
            if (role != RecoveryConsumableRole::None)
            {
                bool retainedFood = item->GetPos() == context.reserves.foodPosition;
                bool retainedDrink = item->GetPos() == context.reserves.drinkPosition;
                if (retainedFood)
                    return { false, false, "protected: retained food reserve" };
                if (retainedDrink)
                    return { false, false, "protected: retained drink reserve" };
                if (role == RecoveryConsumableRole::Drink)
                    return { true, false, "sell: surplus drink stack" };
                return { true, false, "sell: surplus food stack" };
            }
            if (proto->SubClass == ITEM_SUBCLASS_CONSUMABLE_OTHER || proto->SubClass == ITEM_SUBCLASS_SCROLL)
                return { true, false, "sell: surplus scroll / misc consumable" };
            return { false, false, "protected: non-recovery consumable" };
        }

        if (proto->Quality == ITEM_QUALITY_NORMAL)
            return { true, false, "sell: common non-consumable" };

        if (proto->Quality > ITEM_QUALITY_NORMAL)
            return { true, false, "sell: uncommon-or-better surplus item" };

        return { false, false, "protected by inventory policy" };
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
            {
                ++equipped;
                continue;
            }

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

        if (added && Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
        {
            TC_LOG_INFO("server", "[WorldBots] [Inventory] Equipped {} starter bag(s), item {}, for bot '{}'.",
                equipped, bagItemId, bot->GetName());
        }
        return added;
    }
}
