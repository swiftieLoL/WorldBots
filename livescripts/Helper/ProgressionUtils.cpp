#include "ProgressionUtils.h"

#include "Globals/ObjectMgr.h"
#include "DataStores/DBCStores.h"
#include "DataStores/DBCStructure.h"
#include "Diagnostics/BotTrace.h"
#include "EquipmentUtils.h"
#include "InventoryUtils.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Helper
{
    namespace
    {
        struct TalentTarget
        {
            TalentEntry const* talent = nullptr;
            uint8_t desiredRank = 0;
        };

        std::string_view TalentBuildFor(uint8_t playerClass)
        {
            // These solo PvE builds are adapted from the reference playerbots
            // premade specs. They also define the spec used by item weights.
            switch (playerClass)
            {
                case CLASS_WARRIOR: return "3022032023335100102012213231251-305-2033";
                case CLASS_PALADIN: return "050501-05-05232051203331302133231331";
                case CLASS_HUNTER: return "502-035305101230013233135031351-5000002";
                case CLASS_ROGUE: return "00532000523-0252051000035015223100501251";
                case CLASS_PRIEST: return "0503203--325023051223010323152301351";
                case CLASS_DEATH_KNIGHT: return "0055021533303310201020131-305020510002-00522";
                case CLASS_SHAMAN: return "053030152-30305003105021333031131131051";
                case CLASS_MAGE: return "23002303110003--0533030313203100030152231351";
                case CLASS_WARLOCK: return "2350022001123510253500331151--55000005";
                case CLASS_DRUID: return "-553202032322010053120030310511-203503012";
                default: return {};
            }
        }

        std::array<std::string_view, 3> SplitTalentBuild(std::string_view build)
        {
            std::array<std::string_view, 3> tabs{};
            size_t begin = 0;
            for (size_t tab = 0; tab < tabs.size(); ++tab)
            {
                size_t end = build.find('-', begin);
                if (end == std::string_view::npos)
                {
                    tabs[tab] = build.substr(begin);
                    break;
                }
                tabs[tab] = build.substr(begin, end - begin);
                begin = end + 1;
            }
            return tabs;
        }

        uint8_t CurrentTalentRank(Player* bot, TalentEntry const* talent)
        {
            if (!bot || !talent)
                return 0;

            for (uint8_t rank = 0; rank < MAX_TALENT_RANK; ++rank)
            {
                uint32_t spellId = talent->SpellRank[rank];
                if (!spellId || !bot->HasSpell(spellId))
                    return rank;
            }
            return MAX_TALENT_RANK;
        }

        std::array<std::vector<TalentTarget>, 3> ParseTalentBuild(Player* bot)
        {
            std::array<std::vector<TalentTarget>, 3> targets;
            if (!bot)
                return targets;

            std::array<std::vector<TalentEntry const*>, 3> talentsByTab;
            uint32_t classMask = bot->GetClassMask();
            for (uint32_t index = 0; index < sTalentStore.GetNumRows(); ++index)
            {
                TalentEntry const* talent = sTalentStore.LookupEntry(index);
                if (!talent)
                    continue;
                TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TabID);
                if (!tab || (tab->ClassMask & classMask) == 0 || tab->OrderIndex >= talentsByTab.size())
                    continue;
                talentsByTab[tab->OrderIndex].push_back(talent);
            }

            auto buildTabs = SplitTalentBuild(TalentBuildFor(bot->GetClass()));
            for (size_t tab = 0; tab < talentsByTab.size(); ++tab)
            {
                auto& talents = talentsByTab[tab];
                std::sort(talents.begin(), talents.end(), [](TalentEntry const* left, TalentEntry const* right) {
                    return left->TierID != right->TierID
                        ? left->TierID < right->TierID
                        : left->ColumnIndex < right->ColumnIndex;
                });

                size_t count = std::min(talents.size(), buildTabs[tab].size());
                for (size_t index = 0; index < count; ++index)
                {
                    char rank = buildTabs[tab][index];
                    if (rank < '1' || rank > '5')
                        continue;
                    targets[tab].push_back({ talents[index], static_cast<uint8_t>(rank - '0') });
                }
            }
            return targets;
        }

        uint32_t SpendTalentPoints(Player* bot)
        {
            if (!bot || bot->GetFreeTalentPoints() == 0)
                return 0;

            auto targets = ParseTalentBuild(bot);
            std::array<size_t, 3> tabOrder = { 0, 1, 2 };
            std::sort(tabOrder.begin(), tabOrder.end(), [&targets](size_t left, size_t right) {
                auto points = [&targets](size_t tab) {
                    uint32_t result = 0;
                    for (TalentTarget const& target : targets[tab])
                        result += target.desiredRank;
                    return result;
                };
                return points(left) > points(right);
            });

            uint32_t learned = 0;
            while (bot->GetFreeTalentPoints() > 0)
            {
                bool progressed = false;
                for (size_t tab : tabOrder)
                {
                    for (TalentTarget const& target : targets[tab])
                    {
                        uint8_t currentRank = CurrentTalentRank(bot, target.talent);
                        if (currentRank >= target.desiredRank || currentRank >= MAX_TALENT_RANK)
                            continue;

                        uint32_t pointsBefore = bot->GetFreeTalentPoints();
                        bot->LearnTalent(target.talent->ID, currentRank);
                        if (bot->GetFreeTalentPoints() < pointsBefore)
                        {
                            ++learned;
                            progressed = true;
                            break;
                        }
                    }
                    if (progressed)
                        break;
                }
                if (!progressed)
                    break;
            }

            if (learned > 0)
                bot->SendTalentsInfoData(false);
            return learned;
        }

        bool LearnSpellIfAvailable(Player* bot, uint32_t spellId)
        {
            if (!bot || !spellId || bot->HasSpell(spellId))
                return false;
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, bot, false))
                return false;
            bot->LearnSpell(spellId, false);
            return bot->HasSpell(spellId);
        }

        std::pair<uint32_t, uint32_t> RacialGroundMounts(uint8_t race)
        {
            switch (race)
            {
                case RACE_HUMAN: return { 470, 23228 };
                case RACE_ORC: return { 6654, 23250 };
                case RACE_DWARF: return { 6899, 23238 };
                case RACE_NIGHTELF: return { 10789, 23219 };
                case RACE_UNDEAD_PLAYER: return { 17463, 17465 };
                case RACE_TAUREN: return { 18990, 23249 };
                case RACE_GNOME: return { 10969, 23225 };
                case RACE_TROLL: return { 10796, 23241 };
                case RACE_DRAENEI: return { 34406, 35713 };
                case RACE_BLOODELF: return { 33660, 35025 };
                default: return { 470, 23228 };
            }
        }

        uint32_t AcquireMounts(Player* bot)
        {
            if (!bot)
                return 0;

            uint32_t learned = 0;
            auto ground = RacialGroundMounts(bot->GetRace());
            if (bot->GetLevel() >= 20)
            {
                learned += LearnSpellIfAvailable(bot, 33388) ? 1u : 0u;
                learned += LearnSpellIfAvailable(bot, ground.first) ? 1u : 0u;
            }
            if (bot->GetLevel() >= 40)
            {
                learned += LearnSpellIfAvailable(bot, 33391) ? 1u : 0u;
                learned += LearnSpellIfAvailable(bot, ground.second) ? 1u : 0u;
            }
            if (bot->GetLevel() >= 60)
            {
                learned += LearnSpellIfAvailable(bot, 34090) ? 1u : 0u;
                uint32_t flying = bot->GetTeamId() == TEAM_HORDE ? 32244 : 32235;
                learned += LearnSpellIfAvailable(bot, flying) ? 1u : 0u;
            }
            if (bot->GetLevel() >= 70)
            {
                learned += LearnSpellIfAvailable(bot, 34091) ? 1u : 0u;
                uint32_t fastFlying = bot->GetTeamId() == TEAM_HORDE ? 32295 : 32242;
                learned += LearnSpellIfAvailable(bot, fastFlying) ? 1u : 0u;
            }
            return learned;
        }

        struct CriticalSupplySnapshot
        {
            uint32_t food = 0;
            uint32_t drink = 0;
            uint32_t potions = 0;
            uint32_t compatibleAmmo = 0;
            uint32_t equippedAmmoId = 0;
            std::unordered_set<uint32_t> missingReagents;
        };

        CriticalSupplySnapshot InspectCriticalSupplies(Player* bot)
        {
            CriticalSupplySnapshot snapshot;
            if (!bot || !bot->IsInWorld())
                return snapshot;

            float bestAmmoScore = -1.0f;
            InventoryUtils::ForEachBagItem(bot, [&](uint8_t, uint8_t, Item* item) {
                ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
                if (!itemTemplate || bot->CanUseItem(itemTemplate) != EQUIP_ERR_OK)
                    return true;

                RecoveryConsumableRole role = InventoryUtils::GetRecoveryConsumableRole(itemTemplate);
                if (role == RecoveryConsumableRole::Food || role == RecoveryConsumableRole::FoodAndDrink)
                    snapshot.food += item->GetCount();
                if (role == RecoveryConsumableRole::Drink || role == RecoveryConsumableRole::FoodAndDrink)
                    snapshot.drink += item->GetCount();
                if (itemTemplate->Class == ITEM_CLASS_CONSUMABLE && itemTemplate->SubClass == ITEM_SUBCLASS_POTION)
                    snapshot.potions += item->GetCount();
                if (itemTemplate->Class == ITEM_CLASS_PROJECTILE && bot->CheckAmmoCompatibility(itemTemplate))
                {
                    snapshot.compatibleAmmo += item->GetCount();
                    float score = itemTemplate->Damage[0].DamageMin + itemTemplate->Damage[0].DamageMax;
                    if (score > bestAmmoScore)
                    {
                        bestAmmoScore = score;
                        snapshot.equippedAmmoId = itemTemplate->ItemId;
                    }
                }
                return true;
            });

            for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (playerSpell.state == PLAYERSPELL_REMOVED || playerSpell.disabled)
                    continue;
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (!spellInfo)
                    continue;
                for (int32_t reagent : spellInfo->Reagent)
                {
                    uint32_t reagentId = reagent > 0 ? static_cast<uint32_t>(reagent) : 0;
                    if (reagentId != 0 && sObjectMgr->GetItemTemplate(reagentId) &&
                        bot->GetItemCount(reagentId, false) < 5)
                        snapshot.missingReagents.insert(reagentId);
                }
            }
            return snapshot;
        }

        CriticalSupplyDeficit DeficitsFromSnapshot(Player* bot, CriticalSupplySnapshot const& snapshot)
        {
            CriticalSupplyDeficit deficits = CriticalSupplyDeficit::None;
            if (!bot || !bot->IsInWorld())
                return deficits;
            if (snapshot.food < 5)
                deficits |= CriticalSupplyDeficit::Food;
            if (bot->GetMaxPower(POWER_MANA) > 0 && snapshot.drink < 5)
                deficits |= CriticalSupplyDeficit::Drink;
            if (snapshot.potions < 2)
                deficits |= CriticalSupplyDeficit::Potion;
            if (bot->GetClass() == CLASS_HUNTER && snapshot.compatibleAmmo < 200)
            {
                Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
                if (ranged && ranged->GetTemplate() &&
                    (ranged->GetTemplate()->SubClass == ITEM_SUBCLASS_WEAPON_BOW ||
                     ranged->GetTemplate()->SubClass == ITEM_SUBCLASS_WEAPON_GUN ||
                     ranged->GetTemplate()->SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW))
                {
                    deficits |= CriticalSupplyDeficit::Ammunition;
                }
            }
            if (!snapshot.missingReagents.empty())
                deficits |= CriticalSupplyDeficit::Reagent;
            return deficits;
        }

        template <std::size_t Size, typename Predicate>
        uint32_t SelectUsableProvisionItem(Player* bot, std::array<uint32_t, Size> const& candidates,
            Predicate&& predicate)
        {
            for (uint32_t itemId : candidates)
            {
                ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
                if (itemTemplate && bot->CanUseItem(itemTemplate) == EQUIP_ERR_OK && predicate(itemTemplate))
                    return itemId;
            }
            return 0;
        }

        uint32_t CreateItems(Player* bot, uint32_t itemId, uint32_t count, bool& storageBlocked)
        {
            if (!bot || itemId == 0 || count == 0)
                return 0;

            uint32_t attempt = count;
            while (attempt > 0)
            {
                ItemPosCountVec destination;
                if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, itemId, attempt) == EQUIP_ERR_OK)
                {
                    if (Item* item = bot->StoreNewItem(destination, itemId, true))
                    {
                        bot->SendNewItem(item, attempt, true, true, false, false);
                        return attempt;
                    }
                }
                attempt /= 2;
            }
            storageBlocked = true;
            return 0;
        }
    }

    void ProgressionUtils::MaintainCharacter(Player* bot, uint8_t& lastMaintainedLevel)
    {
        if (!bot || !bot->IsInWorld())
            return;

        bool levelChanged = lastMaintainedLevel != bot->GetLevel();
        if (!levelChanged && bot->GetFreeTalentPoints() == 0)
            return;

        lastMaintainedLevel = bot->GetLevel();
        uint32_t talentsLearned = SpendTalentPoints(bot);
        uint32_t mountsLearned = 0;
        if (levelChanged)
        {
            // Autonomous bots should not abandon a weapon upgrade because a
            // newly introduced weapon family is still at skill 1.
            bot->UpdateSkillsForLevel();
            bot->UpdateWeaponsSkillsToMaxSkillsForLevel();
            mountsLearned = AcquireMounts(bot);
        }

        if ((talentsLearned > 0 || mountsLearned > 0) &&
            Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
        {
            BotSpecialization specialization = EquipmentUtils::GetSpecialization(bot);
            TC_LOG_INFO("server", "[WorldBots] [Progression] Bot '{}' maintained {} spec: spent {} talent point(s), learned {} riding/mount spell(s), weapon skills synchronized for level {}.",
                bot->GetName(), EquipmentUtils::GetSpecializationName(specialization), talentsLearned,
                mountsLearned, bot->GetLevel());
        }
    }

    CriticalSupplyDeficit ProgressionUtils::GetCriticalSupplyDeficits(Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return CriticalSupplyDeficit::None;
        return DeficitsFromSnapshot(bot, InspectCriticalSupplies(bot));
    }

    std::string ProgressionUtils::DescribeCriticalSupplyDeficits(CriticalSupplyDeficit deficits)
    {
        if (deficits == CriticalSupplyDeficit::None)
            return "none";
        std::string result;
        auto append = [&result](const char* name) {
            if (!result.empty())
                result += ", ";
            result += name;
        };
        if (HasSupplyDeficit(deficits, CriticalSupplyDeficit::Food)) append("food");
        if (HasSupplyDeficit(deficits, CriticalSupplyDeficit::Drink)) append("drink");
        if (HasSupplyDeficit(deficits, CriticalSupplyDeficit::Potion)) append("potions");
        if (HasSupplyDeficit(deficits, CriticalSupplyDeficit::Ammunition)) append("ammunition");
        if (HasSupplyDeficit(deficits, CriticalSupplyDeficit::Reagent)) append("reagents");
        return result;
    }

    bool ProgressionUtils::HasCriticalSupplyDeficit(Player* bot)
    {
        return GetCriticalSupplyDeficits(bot) != CriticalSupplyDeficit::None;
    }

    bool ProgressionUtils::NeedsCriticalRestock(Player* bot)
    {
        return bot && HasCriticalSupplyDeficit(bot);
    }

    SupplyProvisionResult ProgressionUtils::ProvisionCriticalSupplies(Player* bot)
    {
        SupplyProvisionResult result;
        if (!bot || !bot->IsInWorld())
            return result;

        CriticalSupplySnapshot before = InspectCriticalSupplies(bot);
        result.before = DeficitsFromSnapshot(bot, before);
        if (result.before == CriticalSupplyDeficit::None)
        {
            result.after = result.before;
            return result;
        }

        static constexpr std::array<uint32_t, 8> FoodItems{
            33449, 27854, 8952, 4599, 3770, 2287, 117, 4540
        };
        static constexpr std::array<uint32_t, 8> DrinkItems{
            33444, 28399, 8766, 1645, 1708, 1205, 1179, 159
        };
        static constexpr std::array<uint32_t, 8> PotionItems{
            33447, 22829, 13446, 3928, 1710, 929, 858, 118
        };
        static constexpr std::array<uint32_t, 11> AmmoItems{
            41165, 41164, 28053, 28060, 11285, 11284, 3030, 3033, 2515, 2516, 2512
        };

        auto foodRole = [](ItemTemplate const* itemTemplate) {
            return ProvidesFood(InventoryUtils::GetRecoveryConsumableRole(itemTemplate));
        };
        auto drinkRole = [](ItemTemplate const* itemTemplate) {
            return ProvidesDrink(InventoryUtils::GetRecoveryConsumableRole(itemTemplate));
        };
        auto potionRole = [](ItemTemplate const* itemTemplate) {
            return itemTemplate->Class == ITEM_CLASS_CONSUMABLE &&
                itemTemplate->SubClass == ITEM_SUBCLASS_POTION;
        };
        auto ammoRole = [bot](ItemTemplate const* itemTemplate) {
            return itemTemplate->Class == ITEM_CLASS_PROJECTILE && bot->CheckAmmoCompatibility(itemTemplate);
        };

        if (HasSupplyDeficit(result.before, CriticalSupplyDeficit::Food))
        {
            uint32_t itemId = SelectUsableProvisionItem(bot, FoodItems, foodRole);
            result.itemsCreated += CreateItems(bot, itemId, 20 - std::min(before.food, 20u), result.storageBlocked);
        }
        if (HasSupplyDeficit(result.before, CriticalSupplyDeficit::Drink))
        {
            CriticalSupplySnapshot current = InspectCriticalSupplies(bot);
            uint32_t itemId = SelectUsableProvisionItem(bot, DrinkItems, drinkRole);
            result.itemsCreated += CreateItems(bot, itemId, 20 - std::min(current.drink, 20u), result.storageBlocked);
        }
        if (HasSupplyDeficit(result.before, CriticalSupplyDeficit::Potion))
        {
            uint32_t itemId = SelectUsableProvisionItem(bot, PotionItems, potionRole);
            result.itemsCreated += CreateItems(bot, itemId, 5 - std::min(before.potions, 5u), result.storageBlocked);
        }
        if (HasSupplyDeficit(result.before, CriticalSupplyDeficit::Ammunition))
        {
            uint32_t itemId = SelectUsableProvisionItem(bot, AmmoItems, ammoRole);
            if (itemId != 0)
            {
                result.itemsCreated += CreateItems(bot, itemId,
                    800 - std::min(before.compatibleAmmo, 800u), result.storageBlocked);
                if (bot->GetItemCount(itemId, false) > 0)
                    bot->SetAmmo(itemId);
            }
        }
        if (HasSupplyDeficit(result.before, CriticalSupplyDeficit::Reagent))
        {
            for (uint32_t reagentId : before.missingReagents)
            {
                uint32_t count = bot->GetItemCount(reagentId, false);
                result.itemsCreated += CreateItems(bot, reagentId,
                    5 - std::min(count, 5u), result.storageBlocked);
            }
        }

        result.after = GetCriticalSupplyDeficits(bot);
        return result;
    }

    void ProgressionUtils::EnsureFactionFlightPathsLearned(Player* bot)
    {
        if (!bot)
            return;

        uint8_t teamIndex = static_cast<uint8_t>(bot->GetTeamId());
        if (teamIndex >= 2)
            return;

        uint32_t newlyLearned = 0;
        for (TaxiNodesEntry const* taxi : sTaxiNodesStore)
        {
            if (!taxi || taxi->ID == 0)
                continue;

            if (taxi->MountCreatureID[teamIndex] != 0)
            {
                if (bot->m_taxi.SetTaximaskNode(taxi->ID))
                {
                    ++newlyLearned;
                }
            }
        }

        if (newlyLearned > 0 && Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
        {
            TC_LOG_INFO("server", "[WorldBots] [Progression] Bot '{}' unlocked {} faction flight path(s) from DBC.",
                bot->GetName(), newlyLearned);
        }
    }
}
