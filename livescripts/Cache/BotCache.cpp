#include "BotCache.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Helper/Constants.h"
#include "Helper/CombatUtils.h"
#include "Helper/LiveEcologyPolicy.h"
#include "Helper/MathUtils.h"
#include "Helper/QuestUtils.h"
#include "Helper/ProgressionPolicy.h"
#include "Helper/RecoveryHubPolicy.h"
#include "Helper/VendorSelectionPolicy.h"
#include "Brain/QuestAvailabilityPolicy.h"
#include "Brain/QuestProgressionPolicy.h"
#include "Brain/QuestExclusionPolicy.h"
#include "Config/BotConfig.h"
#include "Diagnostics/BotTrace.h"
#include "DataStores/DBCStores.h"
#include "GameEventMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "Creature.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <deque>
#include <chrono>
#include <unordered_map>
#include <tuple>

namespace Cache
{
    static bool s_isInitialized = false;
    static std::unordered_multimap<uint32_t, PositionInfo> s_npcLocations;
    struct CreatureSpawnInfo
    {
        uint64_t spawnId = 0;
        uint32_t entry = 0;
        PositionInfo position;
        float orientation = 0.0f;
    };
    static std::unordered_map<uint32_t, std::vector<CreatureSpawnInfo>> s_creatureSpawnsByMap;
    static std::unordered_map<uint32_t, std::vector<const CreatureSpawnInfo*>> s_creatureSpawnsByEntry;
    static std::unordered_map<uint64_t, std::vector<int16_t>> s_creatureSpawnEvents;
    static std::unordered_map<uint64_t, std::vector<const CreatureSpawnInfo*>> s_grindCandidates;
    static std::unordered_multimap<uint32_t, PositionInfo> s_goLocations;
    static std::unordered_multimap<uint32_t, uint32_t> s_goLootIdToEntry;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_questEnders;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_questStarters;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_goQuestEnders;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_goQuestStarters;
    static std::unordered_map<uint32_t, std::unordered_set<uint32_t>> s_creatureQuestStarterEntriesByMap;
    static std::unordered_map<uint32_t, std::unordered_set<uint32_t>> s_gameObjectQuestStarterEntriesByMap;
    static std::unordered_map<uint32_t, std::vector<LootSource>> s_itemLootSources;
    static std::unordered_map<uint32_t, std::unordered_set<uint32_t>> s_itemVendorEntries;
    static std::unordered_set<uint32_t> s_castCreditQuests;
    static std::unordered_map<uint32_t, std::vector<SpellInteractionTarget>> s_spellInteractionTargets;
    static std::deque<VendorInfo> s_vendors;
    static std::unordered_map<uint32_t, std::vector<const VendorInfo*>> s_vendorsByMap;
    static std::unordered_map<const VendorInfo*, std::chrono::steady_clock::time_point> s_suppressedVendorLocations;
    static std::unordered_map<const VendorInfo*, uint32_t> s_vendorLocationFailureCounts;
    static const std::vector<uint32_t> s_emptyQuestRelations;

    static void AddOrImproveLootSource(uint32_t itemId, LootSourceType type,
        uint32_t entry, float dropChance)
    {
        if (itemId == 0 || entry == 0)
            return;

        auto& sources = s_itemLootSources[itemId];
        auto existing = std::find_if(sources.begin(), sources.end(),
            [type, entry](const LootSource& source) {
                return source.type == type && source.entry == entry;
            });
        float normalizedChance = std::fabs(dropChance);
        if (existing == sources.end())
            sources.push_back({ type, entry, normalizedChance });
        else
            existing->dropChance = std::max(existing->dropChance, normalizedChance);
    }

    static void LoadCreatureData()
    {
        if (QueryResult result = WorldDatabase.Query(
            "SELECT guid, eventEntry FROM game_event_creature"))
        {
            do
            {
                Field* fields = result->Fetch();
                s_creatureSpawnEvents[fields[0].GetUInt64()].push_back(
                    fields[1].GetInt16());
            } while (result->NextRow());
        }

        // Creature locations and stable spawn identities.
        if (QueryResult result = WorldDatabase.Query("SELECT guid, id, map, position_x, position_y, position_z, orientation FROM creature"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint64_t spawnId = fields[0].GetUInt64();
                uint32_t id = fields[1].GetUInt32();
                PositionInfo pos;
                pos.mapId = fields[2].GetUInt16();
                pos.x = fields[3].GetFloat();
                pos.y = fields[4].GetFloat();
                pos.z = fields[5].GetFloat();
                s_npcLocations.emplace(id, pos);
                s_creatureSpawnsByMap[pos.mapId].push_back({ spawnId, id, pos,
                    fields[6].GetFloat() });
            } while (result->NextRow());
        }

    }

    static void LoadGameObjectData()
    {
        // GameObject locations and loot-template mappings.
        if (QueryResult result = WorldDatabase.Query("SELECT id, map, position_x, position_y, position_z FROM gameobject"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t id = fields[0].GetUInt32();
                PositionInfo pos;
                pos.mapId = fields[1].GetUInt16();
                pos.x = fields[2].GetFloat();
                pos.y = fields[3].GetFloat();
                pos.z = fields[4].GetFloat();
                s_goLocations.emplace(id, pos);
            } while (result->NextRow());
        }

        // 2b. Load GameObject Loot ID Mappings (gameobject_template data1 for Type 3 Chests/Barrels)
        if (QueryResult result = WorldDatabase.Query("SELECT entry, data1 FROM gameobject_template WHERE type = 3 AND data1 > 0"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t goEntry = fields[0].GetUInt32();
                uint32_t lootId = fields[1].GetUInt32();
                s_goLootIdToEntry.emplace(lootId, goEntry);
            } while (result->NextRow());
        }

    }

    static void LoadQuestData()
    {
        // Creature and GameObject quest relations.
        if (QueryResult result = WorldDatabase.Query("SELECT quest, id FROM creature_questender"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t qId = fields[0].GetUInt32();
                uint32_t enderEntry = fields[1].GetUInt32();
                s_questEnders[qId].push_back(enderEntry);
            } while (result->NextRow());
        }

        // 4. Load Quest Starters
        if (QueryResult result = WorldDatabase.Query("SELECT id, quest FROM creature_queststarter"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t starterEntry = fields[0].GetUInt32();
                uint32_t qId = fields[1].GetUInt32();
                s_questStarters[starterEntry].push_back(qId);
            } while (result->NextRow());
        }

        if (QueryResult result = WorldDatabase.Query("SELECT quest, id FROM gameobject_questender"))
        {
            do
            {
                Field* fields = result->Fetch();
                s_goQuestEnders[fields[0].GetUInt32()].push_back(fields[1].GetUInt32());
            } while (result->NextRow());
        }

        if (QueryResult result = WorldDatabase.Query("SELECT id, quest FROM gameobject_queststarter"))
        {
            do
            {
                Field* fields = result->Fetch();
                s_goQuestStarters[fields[0].GetUInt32()].push_back(fields[1].GetUInt32());
            } while (result->NextRow());
        }

        // Preserve the database-authored CAST bit before TrinityCore adds its
        // internal creature-objective flags to every loaded quest template.
        if (QueryResult result = WorldDatabase.Query("SELECT ID FROM quest_template_addon WHERE (SpecialFlags & 32) <> 0"))
        {
            do
            {
                s_castCreditQuests.insert(result->Fetch()[0].GetUInt32());
            } while (result->NextRow());
        }

        // SourceType 17 conditions constrain implicit spell targets. They are
        // the authoritative bridge between a quest source item's use spell,
        // the live entity the player must interact with, and the synthetic
        // creature/GO entry that the quest log ultimately credits.
        if (QueryResult result = WorldDatabase.Query(
            "SELECT SourceEntry, ConditionTarget, ConditionTypeOrReference, "
            "ConditionValue1, ConditionValue2 FROM conditions "
            "WHERE SourceTypeOrReferenceId = 17 AND NegativeCondition = 0 AND "
            "ConditionTypeOrReference IN (29, 30, 31)"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t spellId = fields[0].GetUInt32();
                uint32_t conditionTarget = fields[1].GetUInt32();
                uint32_t conditionType = fields[2].GetUInt32();
                uint32_t value1 = fields[3].GetUInt32();
                uint32_t value2 = fields[4].GetUInt32();

                SpellInteractionTarget target;
                if (conditionType == 31) // CONDITION_OBJECT_ENTRY_GUID
                {
                    // TYPEID_UNIT=3 and TYPEID_GAMEOBJECT=5 in this core.
                    if (value1 != 3 && value1 != 5)
                        continue;
                    target.entry = value2;
                    target.isGameObject = value1 == 5;
                    target.targetsEntity = conditionTarget != 0;
                }
                else
                {
                    target.entry = value1;
                    target.isGameObject = conditionType == 30; // CONDITION_NEAR_GAMEOBJECT
                    target.targetsEntity = false;
                }

                if (spellId == 0 || target.entry == 0)
                    continue;
                auto& targets = s_spellInteractionTargets[spellId];
                if (std::find_if(targets.begin(), targets.end(),
                    [&target](const SpellInteractionTarget& existing) {
                        return existing.entry == target.entry &&
                            existing.isGameObject == target.isGameObject &&
                            existing.targetsEntity == target.targetsEntity;
                    }) == targets.end())
                {
                    targets.push_back(target);
                }
            } while (result->NextRow());
        }

    }

    static void LoadLootData()
    {
        // Creature item loot sources (direct items, excluding reference loot IDs).
        std::unordered_map<uint32_t, std::vector<uint32_t>> creatureLootReferences;

        if (QueryResult result = WorldDatabase.Query("SELECT Entry, Item, Reference, Chance FROM creature_loot_template"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t entry = fields[0].GetUInt32();
                uint32_t item = fields[1].GetUInt32();
                uint32_t reference = fields[2].GetUInt32();
                float chance = fields[3].GetFloat();

                if (reference == 0 && item > 0 && entry > 0)
                {
                    AddOrImproveLootSource(item, LootSourceType::Creature, entry, chance);
                }
                else if (reference > 0 && entry > 0)
                {
                    auto& vec = creatureLootReferences[reference];
                    if (std::find(vec.begin(), vec.end(), entry) == vec.end())
                    {
                        vec.push_back(entry);
                    }
                }
            } while (result->NextRow());
        }

        // 5b. Load reference loot templates, including nested references.
        // Creature and gameobject loot tables can both point at these
        // templates, so resolve them once and apply the result to each source.
        std::unordered_map<uint32_t, std::vector<uint32_t>> referenceItems;
        std::unordered_map<uint32_t, std::vector<uint32_t>> referenceChildren;
        std::unordered_map<uint64_t, float> referenceItemChances;
        if (QueryResult result = WorldDatabase.Query("SELECT Entry, Item, Reference, Chance FROM reference_loot_template"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t refEntry = fields[0].GetUInt32();
                uint32_t item = fields[1].GetUInt32();
                uint32_t childReference = fields[2].GetUInt32();
                float chance = fields[3].GetFloat();

                if (refEntry == 0)
                    continue;

                if (item > 0)
                {
                    auto& items = referenceItems[refEntry];
                    if (std::find(items.begin(), items.end(), item) == items.end())
                        items.push_back(item);
                    uint64_t chanceKey = (static_cast<uint64_t>(refEntry) << 32) | item;
                    referenceItemChances[chanceKey] = std::max(
                        referenceItemChances[chanceKey], std::fabs(chance));
                }

                if (childReference > 0)
                {
                    auto& children = referenceChildren[refEntry];
                    if (std::find(children.begin(), children.end(), childReference) == children.end())
                        children.push_back(childReference);
                }
            } while (result->NextRow());
        }

        auto collectReferenceItems = [&](auto&& self, uint32_t reference, std::unordered_set<uint32_t>& visiting, std::vector<uint32_t>& output) -> void
        {
            if (!visiting.insert(reference).second)
                return; // Protect cache initialization from malformed cycles.

            auto directItems = referenceItems.find(reference);
            if (directItems != referenceItems.end())
            {
                for (uint32_t item : directItems->second)
                {
                    if (std::find(output.begin(), output.end(), item) == output.end())
                        output.push_back(item);
                }
            }

            auto children = referenceChildren.find(reference);
            if (children != referenceChildren.end())
            {
                for (uint32_t child : children->second)
                    self(self, child, visiting, output);
            }

            visiting.erase(reference);
        };

        for (const auto& [reference, creatureEntries] : creatureLootReferences)
        {
            std::vector<uint32_t> items;
            std::unordered_set<uint32_t> visiting;
            collectReferenceItems(collectReferenceItems, reference, visiting, items);

            for (uint32_t item : items)
            {
                uint64_t chanceKey = (static_cast<uint64_t>(reference) << 32) | item;
                float chance = referenceItemChances[chanceKey];
                for (uint32_t creatureEntry : creatureEntries)
                    AddOrImproveLootSource(item, LootSourceType::Creature, creatureEntry, chance);
            }
        }

        // 6. Load GameObject Item Loot Sources (direct and referenced loot).
        std::unordered_map<uint32_t, std::vector<uint32_t>> gameObjectLootReferences;
        if (QueryResult result = WorldDatabase.Query("SELECT Entry, Item, Reference, Chance FROM gameobject_loot_template"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t lootId = fields[0].GetUInt32();
                uint32_t item = fields[1].GetUInt32();
                uint32_t reference = fields[2].GetUInt32();
                float chance = fields[3].GetFloat();

                if (lootId == 0)
                    continue;

                auto range = s_goLootIdToEntry.equal_range(lootId);
                auto addSourceForLootId = [&](uint32_t itemId, float itemChance)
                {
                    if (range.first != range.second)
                    {
                        for (auto it = range.first; it != range.second; ++it)
                        {
                            AddOrImproveLootSource(itemId, LootSourceType::GameObject,
                                it->second, itemChance);
                        }
                    }
                    else
                    {
                        // Keep the old fallback for databases where the loot
                        // table entry is also used as the spawned GO entry.
                        AddOrImproveLootSource(itemId, LootSourceType::GameObject,
                            lootId, itemChance);
                    }
                };

                if (item > 0 && reference == 0)
                    addSourceForLootId(item, chance);
                else if (reference > 0)
                    gameObjectLootReferences[reference].push_back(lootId);
            } while (result->NextRow());
        }

        for (const auto& [reference, lootIds] : gameObjectLootReferences)
        {
            std::vector<uint32_t> items;
            std::unordered_set<uint32_t> visiting;
            collectReferenceItems(collectReferenceItems, reference, visiting, items);

            for (uint32_t lootId : lootIds)
            {
                auto range = s_goLootIdToEntry.equal_range(lootId);
                for (uint32_t item : items)
                {
                    uint64_t chanceKey = (static_cast<uint64_t>(reference) << 32) | item;
                    float chance = referenceItemChances[chanceKey];
                    if (range.first != range.second)
                    {
                        for (auto it = range.first; it != range.second; ++it)
                            AddOrImproveLootSource(item, LootSourceType::GameObject,
                                it->second, chance);
                    }
                    else
                        AddOrImproveLootSource(item, LootSourceType::GameObject,
                            lootId, chance);
                }
            }
        }

    }

    static void LoadVendorData()
    {
        // Load unconditional stationary vendors and repairers. A moving,
        // waypoint-driven, random-wandering, or event-controlled creature's
        // database spawn point is not a dependable live position and must
        // never be used as a long-range destination. Those vendors remain
        // discoverable through VendorAction's live nearby scan.
        if (QueryResult result = WorldDatabase.Query(
            "SELECT c.id, c.map, c.position_x, c.position_y, c.position_z, ct.npcflag "
            "FROM creature c JOIN creature_template ct ON c.id = ct.entry "
            "LEFT JOIN creature_addon ca ON ca.guid = c.guid "
            "LEFT JOIN game_event_creature gec ON gec.guid = c.guid "
            "WHERE (c.id IN (SELECT DISTINCT entry FROM npc_vendor) OR (ct.npcflag & 4096)) "
            "AND (ct.unit_flags & 33554432) = 0 "
            "AND c.MovementType = 0 AND c.wander_distance = 0 "
            "AND COALESCE(ca.path_id, 0) = 0 AND gec.guid IS NULL"))
        {
            do
            {
                Field* fields = result->Fetch();
                VendorInfo v;
                v.entry = fields[0].GetUInt32();
                v.mapId = fields[1].GetUInt16();
                v.x = fields[2].GetFloat();
                v.y = fields[3].GetFloat();
                v.z = fields[4].GetFloat();
                uint32_t flag = fields[5].GetUInt32();
                v.isVendor = (flag & Constants::UnitNpcFlagVendor) != 0;
                v.isRepairer = (flag & Constants::UnitNpcFlagRepair) != 0;
                s_vendors.push_back(v);
            } while (result->NextRow());
        }

        for (const VendorInfo& vendor : s_vendors)
            s_vendorsByMap[vendor.mapId].push_back(&vendor);

        if (QueryResult result = WorldDatabase.Query(
            "SELECT entry, item FROM npc_vendor WHERE item > 0"))
        {
            do
            {
                Field* fields = result->Fetch();
                s_itemVendorEntries[fields[1].GetUInt32()].insert(
                    fields[0].GetUInt32());
            } while (result->NextRow());
        }
    }

    static void BuildZoneIndexes()
    {
        for (const auto& [mapId, spawns] : s_creatureSpawnsByMap)
        {
            (void)mapId;
            for (const CreatureSpawnInfo& spawn : spawns)
                s_creatureSpawnsByEntry[spawn.entry].push_back(&spawn);
        }

        for (const auto& [entry, position] : s_npcLocations)
            if (s_questStarters.contains(entry))
                s_creatureQuestStarterEntriesByMap[position.mapId].insert(entry);
        for (const auto& [entry, position] : s_goLocations)
            if (s_goQuestStarters.contains(entry))
                s_gameObjectQuestStarterEntriesByMap[position.mapId].insert(entry);

    }

    void BotCache::Initialize()
    {
        if (s_isInitialized)
            return;

        if (Diagnostics::BotTrace::ShouldLog(nullptr, Diagnostics::LogEvent::Normal))
            TC_LOG_INFO("server", "[WorldBots] [BotCache] Initializing in-memory static data cache...");

        LoadCreatureData();
        LoadGameObjectData();
        LoadQuestData();
        LoadLootData();
        LoadVendorData();
        BuildZoneIndexes();

        s_isInitialized = true;
        if (Diagnostics::BotTrace::ShouldLog(nullptr, Diagnostics::LogEvent::Normal))
        {
            TC_LOG_INFO("server", "[WorldBots] [BotCache] Static cache populated! (Creature Spawns: {}, GO Spawns: {}, QuestEnders: {}, QuestStarters: {}, LootSources: {}, Vendors: {})",
                s_npcLocations.size(), s_goLocations.size(), s_questEnders.size(), s_questStarters.size(), s_itemLootSources.size(), s_vendors.size());
        }
    }

    bool BotCache::IsInitialized()
    {
        return s_isInitialized;
    }

    template<typename MultiMap>
    static bool FindLocationFromMap(const MultiMap& map, uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId = std::numeric_limits<uint32_t>::max())
    {
        auto range = map.equal_range(entry);
        if (range.first == range.second) return false;

        float bestDistSq = std::numeric_limits<float>::max();
        bool found = false;

        // First pass: filter by nearMapId if specified
        for (auto it = range.first; it != range.second; ++it)
        {
            const auto& pos = it->second;
            if (nearMapId != std::numeric_limits<uint32_t>::max() && pos.mapId != nearMapId) continue;

            if (nearX != 0.0f || nearY != 0.0f)
            {
                float distSq = Helper::DistanceSq2D(pos.x, pos.y, nearX, nearY);
                if (distSq < bestDistSq)
                {
                    bestDistSq = distSq;
                    outX = pos.x;
                    outY = pos.y;
                    outZ = pos.z;
                    outMapId = pos.mapId;
                    found = true;
                }
            }
            else
            {
                outX = pos.x;
                outY = pos.y;
                outZ = pos.z;
                outMapId = pos.mapId;
                return true;
            }
        }

        return found;
    }

    bool BotCache::FindNpcLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId)
    {
        if (!s_isInitialized) Initialize();
        return FindLocationFromMap(s_npcLocations, entry, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId);
    }

    std::vector<PositionInfo> BotCache::GetNpcLocations(uint32_t entry)
    {
        if (!s_isInitialized)
            Initialize();

        std::vector<PositionInfo> locations;
        auto range = s_npcLocations.equal_range(entry);
        for (auto it = range.first; it != range.second; ++it)
            locations.push_back(it->second);
        return locations;
    }

    std::vector<PositionInfo> BotCache::GetGameObjectLocations(uint32_t entry)
    {
        if (!s_isInitialized)
            Initialize();

        std::vector<PositionInfo> locations;
        auto range = s_goLocations.equal_range(entry);
        for (auto it = range.first; it != range.second; ++it)
            locations.push_back(it->second);
        return locations;
    }

    bool BotCache::FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId)
    {
        if (!s_isInitialized) Initialize();
        if (FindLocationFromMap(s_goLocations, entry, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId))
        {
            return true;
        }

        // Fallback: resolve chest loot IDs (data1) to spawned GameObject entries
        auto range = s_goLootIdToEntry.equal_range(entry);
        for (auto it = range.first; it != range.second; ++it)
        {
            if (FindLocationFromMap(s_goLocations, it->second, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId))
            {
                return true;
            }
        }
        return false;
    }

    template <typename Predicate>
    static const VendorInfo* FindNearestNpc(const std::vector<const VendorInfo*>& candidates,
        float botX, float botY, float botZ, float minDistance, Predicate&& predicate)
    {
        const VendorInfo* nearest = nullptr;
        float bestDistSq = std::numeric_limits<float>::max();
        float minDistSq = minDistance * minDistance;
        for (const VendorInfo* candidate : candidates)
        {
            if (!candidate || !predicate(*candidate))
                continue;

            float dx = candidate->x - botX;
            float dy = candidate->y - botY;
            float dz = candidate->z - botZ;
            float dist2DSq = dx * dx + dy * dy;
            float weightedDistSq = Helper::VendorSelectionPolicy::CalculateWeightedDistanceSq(dx, dy, dz);
            if (dist2DSq >= minDistSq && weightedDistSq < bestDistSq)
            {
                bestDistSq = weightedDistSq;
                nearest = candidate;
            }
        }
        return nearest;
    }

    static void CopyVendorResult(const VendorInfo& vendor, uint32_t& outEntry, PositionInfo& outPos)
    {
        outEntry = vendor.entry;
        outPos = { vendor.x, vendor.y, vendor.z, vendor.mapId };
    }

    bool BotCache::FindNearestVendor(uint32_t mapId, float botX, float botY, float botZ,
        bool requireVendor, bool requireRepair, uint32_t& outVendorEntry, PositionInfo& outPos,
        std::function<bool(uint32_t)> entryFilter)
    {
        if (!s_isInitialized) Initialize();

        auto vendorsIt = s_vendorsByMap.find(mapId);
        if (vendorsIt == s_vendorsByMap.end())
            return false;

        auto now = std::chrono::steady_clock::now();
        const VendorInfo* vendor = FindNearestNpc(vendorsIt->second, botX, botY, botZ, 0.0f,
            [&](const VendorInfo& candidate) {
            auto suppressed = s_suppressedVendorLocations.find(&candidate);
            if (suppressed != s_suppressedVendorLocations.end())
            {
                if (now < suppressed->second)
                    return false;
                s_suppressedVendorLocations.erase(suppressed);
            }
            return (!requireVendor || candidate.isVendor) &&
                (!requireRepair || candidate.isRepairer) &&
                (!entryFilter || entryFilter(candidate.entry));
        });
        if (!vendor)
            return false;
        CopyVendorResult(*vendor, outVendorEntry, outPos);
        return true;
    }

    bool BotCache::SuppressVendorLocation(uint32_t entry, uint32_t mapId, float x, float y, float z)
    {
        auto vendorsIt = s_vendorsByMap.find(mapId);
        if (vendorsIt == s_vendorsByMap.end())
            return false;

        bool suppressed = false;
        for (const VendorInfo* vendor : vendorsIt->second)
        {
            if (!vendor || vendor->entry != entry)
                continue;

            float distanceSq = Helper::DistanceSq(vendor->x, vendor->y, vendor->z, x, y, z);
            if (distanceSq > 4.0f)
                continue;

            uint32_t failureCount = ++s_vendorLocationFailureCounts[vendor];
            uint32_t suppressionMinutes = failureCount == 1 ? 5u
                : (failureCount == 2 ? 15u : 60u);
            s_suppressedVendorLocations[vendor] = std::chrono::steady_clock::now() +
                std::chrono::minutes(suppressionMinutes);
            suppressed = true;

            if (Diagnostics::BotTrace::ShouldLog(nullptr, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_WARN("server", "[WorldBots] [BotCache] Vendor Entry {} destination at ({:.1f}, {:.1f}, {:.1f}) Map {} failed validation {} time(s); suppressing this spawn for {} minutes while live discovery and other spawns remain enabled.",
                    entry, x, y, z, mapId, failureCount, suppressionMinutes);
            }
        }
        return suppressed;
    }

    bool BotCache::ConfirmVendorLocation(uint32_t entry, uint32_t mapId, float x, float y, float z)
    {
        auto vendorsIt = s_vendorsByMap.find(mapId);
        if (vendorsIt == s_vendorsByMap.end())
            return false;
        bool restored = false;
        for (const VendorInfo* vendor : vendorsIt->second)
        {
            if (!vendor || vendor->entry != entry ||
                Helper::DistanceSq(vendor->x, vendor->y, vendor->z, x, y, z) > 4.0f)
                continue;
            restored = s_suppressedVendorLocations.erase(vendor) > 0 || restored;
            s_vendorLocationFailureCounts.erase(vendor);
        }
        return restored;
    }

    const std::vector<uint32_t>& BotCache::GetQuestEnders(uint32_t questId)
    {
        if (!s_isInitialized) Initialize();

        auto it = s_questEnders.find(questId);
        if (it != s_questEnders.end())
        {
            return it->second;
        }
        return s_emptyQuestRelations;
    }

    const std::vector<uint32_t>& BotCache::GetQuestStarters(uint32_t creatureEntry)
    {
        if (!s_isInitialized) Initialize();

        auto it = s_questStarters.find(creatureEntry);
        if (it != s_questStarters.end())
        {
            return it->second;
        }
        return s_emptyQuestRelations;
    }

    const std::vector<uint32_t>& BotCache::GetGameObjectQuestEnders(uint32_t questId)
    {
        if (!s_isInitialized) Initialize();
        auto it = s_goQuestEnders.find(questId);
        return it != s_goQuestEnders.end() ? it->second : s_emptyQuestRelations;
    }

    const std::vector<uint32_t>& BotCache::GetGameObjectQuestStarters(uint32_t gameObjectEntry)
    {
        if (!s_isInitialized) Initialize();
        auto it = s_goQuestStarters.find(gameObjectEntry);
        return it != s_goQuestStarters.end() ? it->second : s_emptyQuestRelations;
    }

    std::vector<LootSource> BotCache::GetItemLootSources(uint32_t itemId)
    {
        if (!s_isInitialized) Initialize();

        auto it = s_itemLootSources.find(itemId);
        if (it != s_itemLootSources.end())
        {
            return it->second;
        }
        return {};
    }

    std::vector<VendorInfo> BotCache::GetItemVendorSources(uint32_t itemId)
    {
        if (!s_isInitialized) Initialize();

        std::vector<VendorInfo> result;
        auto entries = s_itemVendorEntries.find(itemId);
        if (entries == s_itemVendorEntries.end())
            return result;
        for (const VendorInfo& vendor : s_vendors)
        {
            if (vendor.isVendor && entries->second.contains(vendor.entry))
                result.push_back(vendor);
        }
        return result;
    }

    bool BotCache::VendorSellsItem(uint32_t vendorEntry, uint32_t itemId)
    {
        if (!s_isInitialized) Initialize();
        auto entries = s_itemVendorEntries.find(itemId);
        return entries != s_itemVendorEntries.end() &&
            entries->second.contains(vendorEntry);
    }

    bool BotCache::IsCastCreditQuest(uint32_t questId)
    {
        return s_castCreditQuests.find(questId) != s_castCreditQuests.end();
    }

    std::vector<SpellInteractionTarget> BotCache::GetSpellInteractionTargets(uint32_t spellId)
    {
        if (!s_isInitialized)
            Initialize();
        auto found = s_spellInteractionTargets.find(spellId);
        return found != s_spellInteractionTargets.end()
            ? found->second : std::vector<SpellInteractionTarget>{};
    }

    bool BotCache::FindNearestAvailableQuestStarter(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
        PositionInfo& outPos, uint32_t& outQuestId, uint32_t& outEntry, bool& outIsGameObject,
        const std::unordered_set<uint32_t>& excludedQuestIds,
        std::function<bool(const PositionInfo&)> positionFilter)
    {
        if (!s_isInitialized || !bot || !bot->IsInWorld()) return false;

        uint32_t bestMapRank = std::numeric_limits<uint32_t>::max();
        uint32_t bestLevelPenalty = std::numeric_limits<uint32_t>::max();
        float bestDistSq = std::numeric_limits<float>::max();
        uint32_t bestQuestId = std::numeric_limits<uint32_t>::max();
        uint32_t bestStarterEntry = std::numeric_limits<uint32_t>::max();
        bool found = false;

        auto evaluateStarter = [&](uint32_t starterEntry, const auto& questIds,
                                   const auto& locations, bool isGameObject) {
                if (!isGameObject)
                {
                    CreatureTemplate const* creature =
                        sObjectMgr->GetCreatureTemplate(starterEntry);
                    FactionTemplateEntry const* botFaction =
                        bot->GetFactionTemplateEntry();
                    FactionTemplateEntry const* starterFaction = creature
                        ? sFactionTemplateStore.LookupEntry(creature->faction) : nullptr;
                    if (!creature ||
                        (creature->npcflag & UNIT_NPC_FLAG_QUESTGIVER) == 0 ||
                        (creature->unit_flags & UNIT_FLAG_UNINTERACTIBLE) != 0 ||
                        !botFaction || !starterFaction ||
                        starterFaction->IsHostileTo(*botFaction) ||
                        botFaction->IsHostileTo(*starterFaction))
                    {
                        return;
                    }
                }

                uint32_t takeableQuestId = 0;
                uint32_t takeableLevelPenalty =
                    std::numeric_limits<uint32_t>::max();
                for (uint32_t qId : questIds)
                {
                    if (excludedQuestIds.find(qId) != excludedQuestIds.end())
                        continue;
                    Quest const* qTemplate = sObjectMgr->GetQuestTemplate(qId);
                    if (bot->GetQuestStatus(qId) == QUEST_STATUS_NONE && qTemplate &&
                        !Brain::IsExcludedQuest(qId, qTemplate->GetZoneOrSort()) &&
                        Helper::IsQuestLevelSuitable(bot->GetLevel(), qTemplate->GetQuestLevel(),
                            Config::BotConfig::GetQuestMaxLevelsAboveBot()) &&
                        bot->CanTakeQuest(qTemplate, false) && bot->CanAddQuest(qTemplate, false) &&
                        Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate))
                    {
                        uint32_t levelPenalty = Brain::QuestLevelFitPenalty(
                            bot->GetLevel(), qTemplate->GetQuestLevel());
                        if (!takeableQuestId ||
                            std::tie(levelPenalty, qId) <
                                std::tie(takeableLevelPenalty, takeableQuestId))
                        {
                            takeableQuestId = qId;
                            takeableLevelPenalty = levelPenalty;
                        }
                    }
                }
                if (!takeableQuestId)
                    return;

                auto considerPosition = [&](const PositionInfo& pos) {
                    if (mapId != std::numeric_limits<uint32_t>::max() && pos.mapId != mapId)
                        return;
                    if (positionFilter && !positionFilter(pos))
                        return;
                    uint32_t mapRank = pos.mapId == bot->GetMapId() ? 0u : 1u;
                    float distSq = Helper::DistanceSq2D(pos.x, pos.y, botX, botY);
                    auto score = std::make_tuple(mapRank,
                        takeableLevelPenalty, distSq, takeableQuestId,
                        starterEntry);
                    auto bestScore = std::make_tuple(bestMapRank,
                        bestLevelPenalty, bestDistSq, bestQuestId,
                        bestStarterEntry);
                    if (!found || score < bestScore)
                    {
                        bestMapRank = mapRank;
                        bestLevelPenalty = takeableLevelPenalty;
                        bestDistSq = distSq;
                        bestQuestId = takeableQuestId;
                        bestStarterEntry = starterEntry;
                        outPos = pos;
                        outQuestId = takeableQuestId;
                        outEntry = starterEntry;
                        outIsGameObject = isGameObject;
                        found = true;
                    }
                };

                if (!isGameObject)
                {
                    auto spawnRange = s_creatureSpawnsByEntry.find(starterEntry);
                    if (spawnRange == s_creatureSpawnsByEntry.end())
                        return;
                    for (const CreatureSpawnInfo* spawn : spawnRange->second)
                    {
                        if (!spawn)
                            continue;
                        auto eventRelations = s_creatureSpawnEvents.find(spawn->spawnId);
                        static const std::vector<int16_t> noEventRelations;
                        const std::vector<int16_t>& entries = eventRelations != s_creatureSpawnEvents.end()
                            ? eventRelations->second : noEventRelations;
                        if (!Brain::IsEventControlledSpawnActive(entries,
                            [](uint16_t eventId) { return IsEventActive(eventId); }))
                        {
                            continue;
                        }
                        considerPosition(spawn->position);
                    }
                    return;
                }

                auto range = locations.equal_range(starterEntry);
                for (auto it = range.first; it != range.second; ++it)
                    considerPosition(it->second);
        };

        auto evaluateStarters = [&](const auto& relations, const auto& locations,
                                    const auto& entriesByMap, bool isGameObject) {
            if (mapId != std::numeric_limits<uint32_t>::max())
            {
                auto indexed = entriesByMap.find(mapId);
                if (indexed == entriesByMap.end())
                    return;
                for (uint32_t starterEntry : indexed->second)
                {
                    auto relation = relations.find(starterEntry);
                    if (relation != relations.end())
                        evaluateStarter(starterEntry, relation->second, locations, isGameObject);
                }
                return;
            }

            for (const auto& [starterEntry, questIds] : relations)
                evaluateStarter(starterEntry, questIds, locations, isGameObject);
        };

        evaluateStarters(s_questStarters, s_npcLocations, s_creatureQuestStarterEntriesByMap, false);
        evaluateStarters(s_goQuestStarters, s_goLocations, s_gameObjectQuestStarterEntriesByMap, true);

        return found;
    }

    bool BotCache::FindNearestSettlementOrVendor(uint32_t mapId, float botX, float botY, float botZ, float minDistance, PositionInfo& outPos, uint32_t& outVendorEntry)
    {
        if (!s_isInitialized) return false;

        auto vendorsIt = s_vendorsByMap.find(mapId);
        if (vendorsIt == s_vendorsByMap.end())
            return false;

        const VendorInfo* vendor = FindNearestNpc(vendorsIt->second, botX, botY, botZ, minDistance,
            [](const VendorInfo&) { return true; });
        if (!vendor)
            return false;
        CopyVendorResult(*vendor, outVendorEntry, outPos);
        return true;
    }

    bool BotCache::FindNearestLevelAppropriateCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId, float minDistance, PositionInfo& outPos, uint32_t& outCreatureEntry)
    {
        return FindNearestGrindingCreature(bot, botX, botY, botZ, mapId, minDistance,
            -2, 3, outPos, outCreatureEntry);
    }

    static const std::vector<const CreatureSpawnInfo*>& GetGrindingCandidates(Player* bot,
        uint32_t mapId, int32_t minLevelOffset, int32_t maxLevelOffset)
    {
        static const std::vector<const CreatureSpawnInfo*> noCandidates;
        if (!s_isInitialized || !bot)
            return noCandidates;

        int32_t minLevel = std::max<int32_t>(1, static_cast<int32_t>(bot->GetLevel()) + minLevelOffset);
        int32_t maxLevel = std::max<int32_t>(minLevel, static_cast<int32_t>(bot->GetLevel()) + maxLevelOffset);

        uint64_t candidateKey = (static_cast<uint64_t>(mapId) << 32) |
            (static_cast<uint64_t>(bot->GetLevel() & 0xFFu) << 24) |
            (static_cast<uint64_t>((minLevelOffset + 128) & 0xFF) << 16) |
            (static_cast<uint64_t>((maxLevelOffset + 128) & 0xFF) << 8);

        auto candidatesIt = s_grindCandidates.find(candidateKey);
        if (candidatesIt == s_grindCandidates.end())
        {
            std::vector<const CreatureSpawnInfo*> candidates;
            auto mapSpawns = s_creatureSpawnsByMap.find(mapId);
            if (mapSpawns != s_creatureSpawnsByMap.end())
            {
                candidates.reserve(mapSpawns->second.size() / 4);
                for (const CreatureSpawnInfo& spawn : mapSpawns->second)
                {
                    CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(spawn.entry);
                    if (!ct)
                        continue;
                    if (ct->type == CREATURE_TYPE_CRITTER || ct->type == CREATURE_TYPE_NON_COMBAT_PET ||
                        ct->rank != CREATURE_ELITE_NORMAL)
                        continue;
                    // Relocation destinations must be plausible grind mobs,
                    // not merely normal-rank creatures in the desired level
                    // band. Town trainers, quest givers, civilians, and
                    // template-level unattackable units otherwise form a
                    // dense ring of false destinations around settlements.
                    constexpr uint32_t NonAttackableTemplateFlags = UNIT_FLAG_NON_ATTACKABLE |
                        UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_NON_ATTACKABLE_2 |
                        UNIT_FLAG_UNINTERACTIBLE;
                    if (ct->npcflag != UNIT_NPC_FLAG_NONE ||
                        (ct->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN) != 0 ||
                        (ct->unit_flags & NonAttackableTemplateFlags) != 0 || ct->RacialLeader)
                        continue;
                    if (static_cast<int32_t>(ct->maxlevel) < minLevel ||
                        static_cast<int32_t>(ct->minlevel) > maxLevel)
                        continue;
                    candidates.push_back(&spawn);
                }
            }
            candidatesIt = s_grindCandidates.emplace(candidateKey, std::move(candidates)).first;
        }

        return candidatesIt->second;
    }

    static bool IsEligibleGrindingSpawn(Player* bot, const CreatureSpawnInfo* spawn,
        const std::unordered_set<uint64_t>& excludedSpawnIds,
        const std::unordered_set<uint32_t>& excludedCreatureEntries,
        const std::function<bool(const PositionInfo&)>& positionFilter,
        GrindingSearchDiagnostics* diagnostics)
    {
        if (!bot || !spawn)
            return false;
        if (excludedSpawnIds.contains(spawn->spawnId) ||
            excludedCreatureEntries.contains(spawn->entry))
        {
            if (diagnostics)
                ++diagnostics->suppressed;
            return false;
        }

        auto eventRelations = s_creatureSpawnEvents.find(spawn->spawnId);
        static const std::vector<int16_t> noEventRelations;
        const std::vector<int16_t>& entries = eventRelations !=
            s_creatureSpawnEvents.end() ? eventRelations->second : noEventRelations;
        if (!Brain::IsEventControlledSpawnActive(entries,
            [](uint16_t eventId) { return IsEventActive(eventId); }))
        {
            if (diagnostics)
                ++diagnostics->inactiveEvent;
            return false;
        }

        CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(spawn->entry);
        FactionTemplateEntry const* botFaction = bot->GetFactionTemplateEntry();
        FactionTemplateEntry const* creatureFaction = creature
            ? sFactionTemplateStore.LookupEntry(creature->faction) : nullptr;
        if (!botFaction || !creatureFaction ||
            creatureFaction->IsFriendlyTo(*botFaction) ||
            botFaction->IsFriendlyTo(*creatureFaction))
        {
            if (diagnostics)
                ++diagnostics->nonHostile;
            return false;
        }

        if (positionFilter && !positionFilter(spawn->position))
        {
            if (diagnostics)
                ++diagnostics->unsafePosition;
            return false;
        }
        return true;
    }

    bool BotCache::FindNearestGrindingCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
        float minDistance, int32_t minLevelOffset, int32_t maxLevelOffset,
        PositionInfo& outPos, uint32_t& outCreatureEntry,
        const std::unordered_set<uint64_t>& excludedSpawnIds,
        const std::unordered_set<uint32_t>& excludedCreatureEntries,
        std::function<bool(const PositionInfo&)> positionFilter,
        float maxDistance, uint64_t* outSpawnId,
        GrindingSearchDiagnostics* diagnostics)
    {
        if (!s_isInitialized || !bot)
            return false;

        if (diagnostics)
            *diagnostics = {};

        float bestDistSq = std::numeric_limits<float>::max();
        float minDistSq = minDistance * minDistance;
        float maxDistSq = maxDistance * maxDistance;
        bool found = false;

        const auto& candidates = GetGrindingCandidates(bot, mapId,
            minLevelOffset, maxLevelOffset);
        if (diagnostics)
            diagnostics->indexedCandidates = static_cast<uint32_t>(candidates.size());
        for (const CreatureSpawnInfo* spawn : candidates)
        {
            if (!IsEligibleGrindingSpawn(bot, spawn, excludedSpawnIds,
                excludedCreatureEntries, positionFilter, diagnostics))
                continue;
            const PositionInfo& pos = spawn->position;
            float distSq = Helper::DistanceSq2D(pos.x, pos.y, botX, botY);
            if (distSq < minDistSq)
            {
                if (diagnostics)
                    ++diagnostics->tooNear;
                continue;
            }
            if (distSq > maxDistSq)
            {
                if (diagnostics)
                    ++diagnostics->tooFar;
                continue;
            }
            if (diagnostics)
                ++diagnostics->eligibleInRange;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                outPos = pos;
                outCreatureEntry = spawn->entry;
                if (outSpawnId)
                    *outSpawnId = spawn->spawnId;
                found = true;
            }
        }

        return found;
    }

    bool BotCache::ResolveLiveGrindingAnchor(Player* bot,
        const PositionInfo& staticAnchor, float searchRadius,
        int32_t minLevelOffset, int32_t maxLevelOffset,
        PositionInfo& outPos, uint32_t& outCreatureEntry,
        uint64_t& outSpawnId,
        const std::unordered_set<uint64_t>& excludedSpawnIds,
        const std::unordered_set<uint32_t>& excludedCreatureEntries,
        std::function<bool(const PositionInfo&)> positionFilter)
    {
        if (!bot || !bot->IsInWorld() || !bot->GetMap() ||
            bot->GetMapId() != staticAnchor.mapId ||
            !std::isfinite(staticAnchor.x) || !std::isfinite(staticAnchor.y) ||
            searchRadius <= 0.0f)
        {
            return false;
        }

        // Remote static spawns are normally not instantiated until their grid
        // is visited. Loading just the nominated grid lets the core apply
        // event, pool, respawn, script, and phase state before we commit to a
        // potentially long movement request. The grid retains its normal
        // unload lifecycle; this does not pin it active.
        Map* map = bot->GetMap();
        map->LoadGrid(staticAnchor.x, staticAnchor.y);

        Creature* best = nullptr;
        float bestDistSq = searchRadius * searchRadius;
        for (const auto& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
        {
            if (!creature || spawnId == 0 ||
                excludedSpawnIds.contains(static_cast<uint64_t>(spawnId)) ||
                excludedCreatureEntries.contains(creature->GetEntry()))
            {
                continue;
            }

            CreatureTemplate const* creatureTemplate =
                creature->GetCreatureTemplate();
            Unit* victim = creature->GetVictim();
            Player* lootRecipient = creature->GetLootRecipient();
            bool unclaimed =
                (!victim || Helper::CombatUtils::IsControlledByBotOrGroupMember(
                    bot, victim)) &&
                (!lootRecipient ||
                    Helper::CombatUtils::IsControlledByBotOrGroupMember(
                        bot, lootRecipient));
            bool normalGrindCreature = creatureTemplate &&
                creatureTemplate->rank == CREATURE_ELITE_NORMAL &&
                !creature->IsCritter() && !creature->IsCivilian();
            bool levelSuitable = normalGrindCreature &&
                Helper::IsGrindingLevelSuitable(bot->GetLevel(),
                    creature->GetLevel(), minLevelOffset, maxLevelOffset);

            PositionInfo livePosition{ creature->GetPositionX(),
                creature->GetPositionY(), creature->GetPositionZ(),
                creature->GetMapId() };
            bool permittedByAction = !positionFilter ||
                positionFilter(livePosition);
            Helper::LiveEcologyPolicy::CandidateState state{
                creature->IsAlive(), creature->IsInWorld(),
                creature->InSamePhase(bot),
                creature->isTargetableForAttack() &&
                    bot->IsValidAttackTarget(creature) &&
                    !creature->IsInEvadeMode(),
                normalGrindCreature, levelSuitable, unclaimed,
                permittedByAction
            };
            if (!Helper::LiveEcologyPolicy::IsEligible(state))
                continue;

            float distSq = Helper::DistanceSq2D(livePosition.x,
                livePosition.y, staticAnchor.x, staticAnchor.y);
            if (distSq > bestDistSq)
                continue;

            best = creature;
            bestDistSq = distSq;
        }

        if (!best)
            return false;

        outPos = { best->GetPositionX(), best->GetPositionY(),
            best->GetPositionZ(), best->GetMapId() };
        outCreatureEntry = best->GetEntry();
        outSpawnId = static_cast<uint64_t>(best->GetSpawnId());
        return true;
    }

    bool BotCache::FindViableGrindingArea(Player* bot, float botX, float botY, float botZ,
        uint32_t mapId, uint32_t currentZoneId, float minDistance,
        int32_t minLevelOffset, int32_t maxLevelOffset,
        PositionInfo& outPos, uint32_t& outCreatureEntry, uint64_t& outSpawnId,
        const std::unordered_set<uint64_t>& excludedSpawnIds,
        const std::unordered_set<uint32_t>& excludedCreatureEntries,
        std::function<bool(const PositionInfo&)> positionFilter,
        GrindingSearchDiagnostics* diagnostics)
    {
        if (!s_isInitialized || !bot || !bot->GetMap() || bot->GetMapId() != mapId)
            return false;

        if (diagnostics)
            *diagnostics = {};

        // Treat a compact population of suitable creatures as a viable hunting
        // area. Grouping by both authored zone and a small spatial cell avoids
        // selecting an isolated spawn merely because its broad zone is busy.
        constexpr float EcologyCellSize = 300.0f;
        constexpr uint32_t MinimumEcologyPopulation = 6;
        struct EcologyCell
        {
            uint32_t zoneId = 0;
            int32_t gridX = 0;
            int32_t gridY = 0;
            uint32_t population = 0;
            const CreatureSpawnInfo* anchor = nullptr;
            float anchorDistanceSq = std::numeric_limits<float>::max();
        };
        struct EcologyCellKey
        {
            uint32_t zoneId = 0;
            int32_t gridX = 0;
            int32_t gridY = 0;

            bool operator==(const EcologyCellKey&) const = default;
        };
        struct EcologyCellKeyHash
        {
            std::size_t operator()(const EcologyCellKey& key) const
            {
                uint64_t mixed = static_cast<uint64_t>(key.zoneId) *
                    0x9E3779B97F4A7C15ULL;
                mixed ^= static_cast<uint64_t>(static_cast<uint32_t>(key.gridX)) << 32;
                mixed ^= static_cast<uint32_t>(key.gridY);
                return static_cast<std::size_t>(mixed);
            }
        };

        float minDistSq = minDistance * minDistance;
        int32_t minLevel = std::max<int32_t>(1,
            static_cast<int32_t>(bot->GetLevel()) + minLevelOffset);
        int32_t maxLevel = std::max<int32_t>(minLevel,
            static_cast<int32_t>(bot->GetLevel()) + maxLevelOffset);
        std::unordered_map<EcologyCellKey, EcologyCell, EcologyCellKeyHash> cells;
        const auto& candidates = GetGrindingCandidates(bot, mapId,
            minLevelOffset, maxLevelOffset);
        if (diagnostics)
            diagnostics->indexedCandidates = static_cast<uint32_t>(candidates.size());
        for (const CreatureSpawnInfo* spawn : candidates)
        {
            if (!IsEligibleGrindingSpawn(bot, spawn, excludedSpawnIds,
                excludedCreatureEntries, positionFilter, diagnostics))
            {
                if (diagnostics)
                    ++diagnostics->weakLevelFit;
                continue;
            }

            CreatureTemplate const* creature =
                sObjectMgr->GetCreatureTemplate(spawn->entry);
            if (!creature)
                continue;
            float authoredMidLevel =
                (static_cast<float>(creature->minlevel) +
                 static_cast<float>(creature->maxlevel)) * 0.5f;
            if (authoredMidLevel < static_cast<float>(minLevel) ||
                authoredMidLevel > static_cast<float>(maxLevel))
            {
                // A marginal template-range overlap is sufficient for a local
                // lead, but not strong enough evidence to relocate zones. For
                // example, a 5-6 creature should not make a level-5 area look
                // reliably viable when half its live spawns will be rejected.
                continue;
            }

            const PositionInfo& pos = spawn->position;
            float distSq = Helper::DistanceSq2D(pos.x, pos.y, botX, botY);
            if (distSq < minDistSq)
            {
                if (diagnostics)
                    ++diagnostics->tooNear;
                continue;
            }
            if (diagnostics)
                ++diagnostics->eligibleInRange;

            uint32_t zoneId = bot->GetMap()->GetZoneId(bot->GetPhaseMask(),
                pos.x, pos.y, pos.z);
            int32_t gridX = static_cast<int32_t>(std::floor(pos.x / EcologyCellSize));
            int32_t gridY = static_cast<int32_t>(std::floor(pos.y / EcologyCellSize));
            EcologyCell& cell = cells[{ zoneId, gridX, gridY }];
            cell.zoneId = zoneId;
            cell.gridX = gridX;
            cell.gridY = gridY;
            ++cell.population;
            if (distSq < cell.anchorDistanceSq)
            {
                cell.anchor = spawn;
                cell.anchorDistanceSq = distSq;
            }
        }

        const EcologyCell* bestCell = nullptr;
        float bestScore = std::numeric_limits<float>::max();
        uint64_t botSeed = static_cast<uint64_t>(bot->GetGUID().GetCounter());
        for (const auto& [key, cell] : cells)
        {
            if (!cell.anchor || cell.population < MinimumEcologyPopulation)
                continue;
            if (diagnostics)
                ++diagnostics->viableCells;

            // Prefer leaving the barren zone, while still allowing a dense
            // pocket elsewhere in a very large zone. Stable per-bot jitter
            // prevents a group of bots from converging on one identical cell.
            float sameZonePenalty = currentZoneId != 0 && cell.zoneId == currentZoneId
                ? 2.0f : 1.0f;
            uint64_t mixed = static_cast<uint64_t>(EcologyCellKeyHash{}(key)) ^
                (botSeed * 0xBF58476D1CE4E5B9ULL);
            float jitter = 0.90f + static_cast<float>(mixed % 201ULL) / 1000.0f;
            float densityBenefit = std::sqrt(static_cast<float>(cell.population));
            float score = std::sqrt(cell.anchorDistanceSq) * sameZonePenalty * jitter /
                densityBenefit;
            if (score < bestScore)
            {
                bestScore = score;
                bestCell = &cell;
            }
        }

        if (!bestCell)
            return false;

        outPos = bestCell->anchor->position;
        outCreatureEntry = bestCell->anchor->entry;
        outSpawnId = bestCell->anchor->spawnId;
        return true;
    }

    bool BotCache::FindNearestSafeRecoveryHub(Player* bot, uint32_t mapId,
        float botX, float botY, PositionInfo& outPos, uint32_t& outCreatureEntry,
        const std::vector<PositionInfo>& excludedPositions,
        bool requireSuitableEcology)
    {
        if (!s_isInitialized)
            Initialize();
        if (!bot)
            return false;

        FactionTemplateEntry const* botFaction = bot->GetFactionTemplateEntry();
        auto mapSpawns = s_creatureSpawnsByMap.find(mapId);
        if (!botFaction || mapSpawns == s_creatureSpawnsByMap.end())
            return false;

        float bestDistSq = std::numeric_limits<float>::max();
        bool bestHasSuitableCreatures = false;
        bool bestIsInnkeeper = false;
        bool found = false;
        for (const CreatureSpawnInfo& spawn : mapSpawns->second)
        {
            CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(spawn.entry);
            if (!creature ||
                (creature->npcflag & (UNIT_NPC_FLAG_FLIGHTMASTER |
                    UNIT_NPC_FLAG_INNKEEPER)) == 0)
                continue;

            FactionTemplateEntry const* creatureFaction =
                sFactionTemplateStore.LookupEntry(creature->faction);
            if (!creatureFaction ||
                (!creatureFaction->IsFriendlyTo(*botFaction) &&
                 !botFaction->IsFriendlyTo(*creatureFaction)))
            {
                continue;
            }

            const PositionInfo& position = spawn.position;
            if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                !std::isfinite(position.z))
            {
                continue;
            }

            uint32_t areaLevel = 0;
            uint32_t areaId = 0;
            uint32_t zoneId = 0;
            sMapMgr->GetZoneAndAreaId(bot->GetPhaseMask(), zoneId, areaId, mapId, position.x, position.y, position.z);
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
            AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
            int32_t authoredLevel = area && area->ExplorationLevel > 0
                ? area->ExplorationLevel
                : (zone ? zone->ExplorationLevel : 0);
            areaLevel = authoredLevel > 0
                ? static_cast<uint32_t>(authoredLevel) : 0;
            if (!Helper::RecoveryHubPolicy::CanRecoverToHub(
                bot->GetLevel(), mapId, zoneId, areaId, requireSuitableEcology))
            {
                continue;
            }
            if (!Helper::RecoveryHubPolicy::IsZoneLevelSafe(
                bot->GetLevel(), areaLevel))
            {
                continue;
            }

            constexpr float LocalEcologyRadius = 1800.0f;
            constexpr float ImmediateDangerRadius = 250.0f;
            float ecologyRadiusSq = LocalEcologyRadius * LocalEcologyRadius;
            float immediateDangerRadiusSq = ImmediateDangerRadius * ImmediateDangerRadius;
            int32_t grindMinOffset = Config::BotConfig::GetGrindMinLevelOffset();
            int32_t grindMaxOffset = Config::BotConfig::GetGrindMaxLevelOffset();
            bool hasHostileCreatures = false;
            bool hasSuitableCreatures = false;
            bool hasImmediateHighLevelDanger = false;
            for (const CreatureSpawnInfo& localSpawn : mapSpawns->second)
            {
                CreatureTemplate const* local =
                    sObjectMgr->GetCreatureTemplate(localSpawn.entry);
                if (!local || local->rank != CREATURE_ELITE_NORMAL ||
                    local->npcflag != UNIT_NPC_FLAG_NONE ||
                    (local->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN) != 0)
                {
                    continue;
                }

                FactionTemplateEntry const* localFaction =
                    sFactionTemplateStore.LookupEntry(local->faction);
                if (!localFaction ||
                    (!localFaction->IsHostileTo(*botFaction) &&
                     !botFaction->IsHostileTo(*localFaction)))
                {
                    continue;
                }

                float localDistSq = Helper::DistanceSq2D(localSpawn.position.x,
                    localSpawn.position.y, position.x, position.y);
                if (localDistSq > ecologyRadiusSq)
                    continue;

                hasHostileCreatures = true;
                bool suitable = Helper::RecoveryHubPolicy::LevelRangeIntersects(
                    local->minlevel, local->maxlevel, bot->GetLevel(),
                    grindMinOffset, grindMaxOffset);
                hasSuitableCreatures = hasSuitableCreatures || suitable;
                if (!suitable && localDistSq <= immediateDangerRadiusSq &&
                    static_cast<int32_t>(local->minlevel) >
                        static_cast<int32_t>(bot->GetLevel()) + grindMaxOffset)
                {
                    hasImmediateHighLevelDanger = true;
                    break;
                }
            }
            if (hasImmediateHighLevelDanger)
                continue;
            // Custom areas often leave area_level at zero. When they do have
            // local combat content, require that ecology to contain something
            // inside the configured grind band instead of treating zero as an
            // unrestricted level bracket. A peaceful hub with no nearby
            // hostiles remains a valid last-resort recovery anchor.
            if (areaLevel == 0 && hasHostileCreatures && !hasSuitableCreatures)
                continue;
            if (requireSuitableEcology && !hasSuitableCreatures)
                continue;

            PositionInfo candidatePosition = position;
            constexpr float HubClearance = 5.0f;
            candidatePosition.x += std::cos(spawn.orientation) * HubClearance;
            candidatePosition.y += std::sin(spawn.orientation) * HubClearance;
            bool excluded = std::any_of(excludedPositions.begin(),
                excludedPositions.end(), [&](const PositionInfo& rejected) {
                    return rejected.mapId == candidatePosition.mapId &&
                        Helper::DistanceSq2D(rejected.x, rejected.y,
                            candidatePosition.x, candidatePosition.y) <= 100.0f;
                });
            if (excluded)
                continue;

            // The bot may be hundreds of yards below valid terrain, so Z must
            // not influence which geographic recovery anchor is nearest.
            float distSq = Helper::DistanceSq2D(position.x, position.y, botX, botY);
            bool isInnkeeper = (creature->npcflag & UNIT_NPC_FLAG_INNKEEPER) != 0;
            if (Helper::RecoveryHubPolicy::ShouldPreferCandidate(found,
                hasSuitableCreatures, bestHasSuitableCreatures, isInnkeeper,
                bestIsInnkeeper, distSq, bestDistSq))
            {
                bestDistSq = distSq;
                bestHasSuitableCreatures = hasSuitableCreatures;
                bestIsInnkeeper = isInnkeeper;
                outPos = candidatePosition;
                outCreatureEntry = spawn.entry;
                found = true;
            }
        }

        return found;
    }
}
