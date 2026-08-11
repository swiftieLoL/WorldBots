#include "BotCache.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Helper/Constants.h"
#include "Helper/MathUtils.h"
#include "Helper/QuestUtils.h"
#include "Helper/ProgressionPolicy.h"
#include "Config/BotConfig.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <deque>

namespace Cache
{
    static bool s_isInitialized = false;
    static std::unordered_multimap<uint32_t, PositionInfo> s_npcLocations;
    struct CreatureSpawnInfo
    {
        uint32_t entry = 0;
        PositionInfo position;
    };
    static std::unordered_map<uint32_t, std::vector<CreatureSpawnInfo>> s_creatureSpawnsByMap;
    static std::unordered_map<uint64_t, std::vector<const CreatureSpawnInfo*>> s_grindCandidates;
    static std::unordered_multimap<uint32_t, PositionInfo> s_goLocations;
    static std::unordered_multimap<uint32_t, uint32_t> s_goLootIdToEntry;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_questEnders;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_questStarters;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_goQuestEnders;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> s_goQuestStarters;
    static std::unordered_map<uint32_t, std::vector<LootSource>> s_itemLootSources;
    static std::unordered_set<uint32_t> s_castCreditQuests;
    static std::deque<VendorInfo> s_vendors;
    static std::unordered_map<uint32_t, std::vector<const VendorInfo*>> s_vendorsByMap;
    static std::unordered_set<const VendorInfo*> s_suppressedVendorLocations;

    void BotCache::Initialize()
    {
        if (s_isInitialized) return;

        TC_LOG_INFO("server", "[WorldBots] [BotCache] Initializing in-memory static data cache...");

        // 1. Load Creature Locations
        if (QueryResult result = WorldDatabase.Query("SELECT id, map, position_x, position_y, position_z FROM creature"))
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
                s_npcLocations.emplace(id, pos);
                s_creatureSpawnsByMap[pos.mapId].push_back({ id, pos });
            } while (result->NextRow());
        }

        // 2. Load GameObject Locations
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

        // 3. Load Quest Enders
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

        // 5. Load Creature Item Loot Sources (Direct items, excluding reference loot IDs)
        std::unordered_map<uint32_t, std::vector<uint32_t>> creatureLootReferences;

        if (QueryResult result = WorldDatabase.Query("SELECT Entry, Item, Reference FROM creature_loot_template"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t entry = fields[0].GetUInt32();
                uint32_t item = fields[1].GetUInt32();
                uint32_t reference = fields[2].GetUInt32();

                if (reference == 0 && item > 0 && entry > 0)
                {
                    LootSource src{ LootSourceType::Creature, entry };
                    auto& vec = s_itemLootSources[item];
                    if (std::find(vec.begin(), vec.end(), src) == vec.end())
                    {
                        vec.push_back(src);
                    }
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
        if (QueryResult result = WorldDatabase.Query("SELECT Entry, Item, Reference FROM reference_loot_template"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t refEntry = fields[0].GetUInt32();
                uint32_t item = fields[1].GetUInt32();
                uint32_t childReference = fields[2].GetUInt32();

                if (refEntry == 0)
                    continue;

                if (item > 0)
                {
                    auto& items = referenceItems[refEntry];
                    if (std::find(items.begin(), items.end(), item) == items.end())
                        items.push_back(item);
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
                auto& vec = s_itemLootSources[item];
                for (uint32_t creatureEntry : creatureEntries)
                {
                    LootSource src{ LootSourceType::Creature, creatureEntry };
                    if (std::find(vec.begin(), vec.end(), src) == vec.end())
                    {
                        vec.push_back(src);
                    }
                }
            }
        }

        // 6. Load GameObject Item Loot Sources (direct and referenced loot).
        std::unordered_map<uint32_t, std::vector<uint32_t>> gameObjectLootReferences;
        if (QueryResult result = WorldDatabase.Query("SELECT Entry, Item, Reference FROM gameobject_loot_template"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t lootId = fields[0].GetUInt32();
                uint32_t item = fields[1].GetUInt32();
                uint32_t reference = fields[2].GetUInt32();

                if (lootId == 0)
                    continue;

                auto range = s_goLootIdToEntry.equal_range(lootId);
                auto addSourceForLootId = [&](uint32_t itemId)
                {
                    if (range.first != range.second)
                    {
                        for (auto it = range.first; it != range.second; ++it)
                        {
                            LootSource src{ LootSourceType::GameObject, it->second };
                            auto& vec = s_itemLootSources[itemId];
                            if (std::find(vec.begin(), vec.end(), src) == vec.end())
                                vec.push_back(src);
                        }
                    }
                    else
                    {
                        // Keep the old fallback for databases where the loot
                        // table entry is also used as the spawned GO entry.
                        LootSource src{ LootSourceType::GameObject, lootId };
                        auto& vec = s_itemLootSources[itemId];
                        if (std::find(vec.begin(), vec.end(), src) == vec.end())
                            vec.push_back(src);
                    }
                };

                if (item > 0 && reference == 0)
                    addSourceForLootId(item);
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
                    if (range.first != range.second)
                    {
                        for (auto it = range.first; it != range.second; ++it)
                        {
                            LootSource src{ LootSourceType::GameObject, it->second };
                            auto& vec = s_itemLootSources[item];
                            if (std::find(vec.begin(), vec.end(), src) == vec.end())
                                vec.push_back(src);
                        }
                    }
                    else
                    {
                        LootSource src{ LootSourceType::GameObject, lootId };
                        auto& vec = s_itemLootSources[item];
                        if (std::find(vec.begin(), vec.end(), src) == vec.end())
                            vec.push_back(src);
                    }
                }
            }
        }

        // 7. Load unconditional stationary Vendors & Repairers. A moving,
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

        s_isInitialized = true;

        TC_LOG_INFO("server", "[WorldBots] [BotCache] Static cache populated! (Creature Spawns: {}, GO Spawns: {}, QuestEnders: {}, QuestStarters: {}, LootSources: {}, Vendors: {})",
            s_npcLocations.size(), s_goLocations.size(), s_questEnders.size(), s_questStarters.size(), s_itemLootSources.size(), s_vendors.size());
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

        if (found) return true;

        // When a map was explicitly supplied, never silently return a spawn
        // from another map. The caller can classify travel instead.
        if (nearMapId != std::numeric_limits<uint32_t>::max())
            return false;

        // Fallback pass: any map only when the caller did not specify one.
        for (auto it = range.first; it != range.second; ++it)
        {
            const auto& pos = it->second;
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

    bool BotCache::FindNpcLocationByName(const char* name, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ)
    {
        if (!name || !*name)
            return false;

        if (!s_isInitialized)
            Initialize();

        std::string needle(name);
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        float bestDistanceSq = std::numeric_limits<float>::max();
        bool found = false;

        for (const auto& [entry, pos] : s_npcLocations)
        {
            CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(entry);
            if (!creatureTemplate)
                continue;

            std::string creatureName = creatureTemplate->Name;
            std::transform(creatureName.begin(), creatureName.end(), creatureName.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (creatureName.find(needle) == std::string::npos)
                continue;

            if (nearX == 0.0f && nearY == 0.0f)
            {
                outX = pos.x;
                outY = pos.y;
                outZ = pos.z;
                outMapId = pos.mapId;
                return true;
            }

            float distanceSq = Helper::DistanceSq2D(pos.x, pos.y, nearX, nearY);
            if (distanceSq < bestDistanceSq)
            {
                bestDistanceSq = distanceSq;
                outX = pos.x;
                outY = pos.y;
                outZ = pos.z;
                outMapId = pos.mapId;
                found = true;
            }
        }

        return found;
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

    bool BotCache::FindNearestVendor(uint32_t mapId, float botX, float botY, float botZ,
        bool requireVendor, bool requireRepair, uint32_t& outVendorEntry, PositionInfo& outPos)
    {
        if (!s_isInitialized) Initialize();

        float bestDistSq = std::numeric_limits<float>::max();
        bool found = false;

        auto vendorsIt = s_vendorsByMap.find(mapId);
        if (vendorsIt == s_vendorsByMap.end())
            return false;

        for (const VendorInfo* vendor : vendorsIt->second)
        {
            if (!vendor) continue;
            if (s_suppressedVendorLocations.find(vendor) != s_suppressedVendorLocations.end()) continue;
            if (requireVendor && !vendor->isVendor) continue;
            if (requireRepair && !vendor->isRepairer) continue;

            float distSq = Helper::DistanceSq2D(vendor->x, vendor->y, botX, botY);

            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                outVendorEntry = vendor->entry;
                outPos.mapId = vendor->mapId;
                outPos.x = vendor->x;
                outPos.y = vendor->y;
                outPos.z = vendor->z;
                found = true;
            }
        }
        return found;
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

            suppressed = s_suppressedVendorLocations.insert(vendor).second || suppressed;
        }

        if (suppressed)
        {
            TC_LOG_WARN("server", "[WorldBots] [BotCache] Suppressed stale Vendor Entry {} destination at ({:.1f}, {:.1f}, {:.1f}) Map {}; live nearby discovery remains enabled.",
                entry, x, y, z, mapId);
        }
        return suppressed;
    }

    std::vector<uint32_t> BotCache::GetQuestEnders(uint32_t questId)
    {
        if (!s_isInitialized) Initialize();

        auto it = s_questEnders.find(questId);
        if (it != s_questEnders.end())
        {
            return it->second;
        }
        return {};
    }

    std::vector<uint32_t> BotCache::GetQuestStarters(uint32_t creatureEntry)
    {
        if (!s_isInitialized) Initialize();

        auto it = s_questStarters.find(creatureEntry);
        if (it != s_questStarters.end())
        {
            return it->second;
        }
        return {};
    }

    std::vector<uint32_t> BotCache::GetGameObjectQuestEnders(uint32_t questId)
    {
        if (!s_isInitialized) Initialize();
        auto it = s_goQuestEnders.find(questId);
        return it != s_goQuestEnders.end() ? it->second : std::vector<uint32_t>{};
    }

    std::vector<uint32_t> BotCache::GetGameObjectQuestStarters(uint32_t gameObjectEntry)
    {
        if (!s_isInitialized) Initialize();
        auto it = s_goQuestStarters.find(gameObjectEntry);
        return it != s_goQuestStarters.end() ? it->second : std::vector<uint32_t>{};
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

    uint32_t BotCache::GetItemLootSource(uint32_t itemId)
    {
        auto sources = GetItemLootSources(itemId);
        if (!sources.empty()) return sources.front().entry;
        return 0;
    }

    bool BotCache::IsCastCreditQuest(uint32_t questId)
    {
        return s_castCreditQuests.find(questId) != s_castCreditQuests.end();
    }

    bool BotCache::FindNearestAvailableQuestStarter(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
        PositionInfo& outPos, uint32_t& outQuestId, uint32_t& outEntry, bool& outIsGameObject,
        const std::unordered_set<uint32_t>& excludedQuestIds)
    {
        if (!s_isInitialized || !bot || !bot->IsInWorld()) return false;

        float bestDistSq = std::numeric_limits<float>::max();
        bool found = false;

        auto evaluateStarters = [&](const auto& relations, const auto& locations, bool isGameObject) {
            for (const auto& [starterEntry, questIds] : relations)
            {
                uint32_t takeableQuestId = 0;
                for (uint32_t qId : questIds)
                {
                    if (excludedQuestIds.find(qId) != excludedQuestIds.end())
                        continue;
                    Quest const* qTemplate = sObjectMgr->GetQuestTemplate(qId);
                    if (bot->GetQuestStatus(qId) == QUEST_STATUS_NONE && qTemplate &&
                        Helper::IsQuestLevelSuitable(bot->GetLevel(), qTemplate->GetQuestLevel(),
                            Config::BotConfig::GetQuestMaxLevelsAboveBot()) &&
                        bot->CanTakeQuest(qTemplate, false) && bot->CanAddQuest(qTemplate, false) &&
                        Helper::QuestUtils::CanReceiveQuestSourceItem(bot, qTemplate))
                    {
                        takeableQuestId = qId;
                        break;
                    }
                }
                if (!takeableQuestId)
                    continue;

                auto range = locations.equal_range(starterEntry);
                for (auto it = range.first; it != range.second; ++it)
                {
                    const auto& pos = it->second;
                    if (pos.mapId != mapId)
                        continue;
                    float distSq = Helper::DistanceSq2D(pos.x, pos.y, botX, botY);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        outPos = pos;
                        outQuestId = takeableQuestId;
                        outEntry = starterEntry;
                        outIsGameObject = isGameObject;
                        found = true;
                    }
                }
            }
        };

        evaluateStarters(s_questStarters, s_npcLocations, false);
        evaluateStarters(s_goQuestStarters, s_goLocations, true);

        return found;
    }

    bool BotCache::FindNearestSettlementOrVendor(uint32_t mapId, float botX, float botY, float botZ, float minDistance, PositionInfo& outPos, uint32_t& outVendorEntry)
    {
        if (!s_isInitialized) return false;

        float bestDistSq = std::numeric_limits<float>::max();
        float minDistSq = minDistance * minDistance;
        bool found = false;

        auto vendorsIt = s_vendorsByMap.find(mapId);
        if (vendorsIt == s_vendorsByMap.end())
            return false;

        for (const VendorInfo* vendor : vendorsIt->second)
        {
            if (!vendor) continue;

            float distSq = Helper::DistanceSq2D(vendor->x, vendor->y, botX, botY);
            if (distSq >= minDistSq && distSq < bestDistSq)
            {
                bestDistSq = distSq;
                outVendorEntry = vendor->entry;
                outPos.mapId = vendor->mapId;
                outPos.x = vendor->x;
                outPos.y = vendor->y;
                outPos.z = vendor->z;
                found = true;
            }
        }
        return found;
    }

    bool BotCache::FindNearestLevelAppropriateCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId, float minDistance, PositionInfo& outPos, uint32_t& outCreatureEntry)
    {
        return FindNearestGrindingCreature(bot, botX, botY, botZ, mapId, minDistance,
            -2, 3, outPos, outCreatureEntry);
    }

    bool BotCache::FindNearestGrindingCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
        float minDistance, int32_t minLevelOffset, int32_t maxLevelOffset,
        PositionInfo& outPos, uint32_t& outCreatureEntry)
    {
        if (!s_isInitialized || !bot) return false;

        int32_t minLevel = std::max<int32_t>(1, static_cast<int32_t>(bot->GetLevel()) + minLevelOffset);
        int32_t maxLevel = std::max<int32_t>(minLevel, static_cast<int32_t>(bot->GetLevel()) + maxLevelOffset);

        float bestDistSq = std::numeric_limits<float>::max();
        float minDistSq = minDistance * minDistance;
        bool found = false;

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
                    if (static_cast<int32_t>(ct->maxlevel) < minLevel ||
                        static_cast<int32_t>(ct->minlevel) > maxLevel)
                        continue;
                    candidates.push_back(&spawn);
                }
            }
            candidatesIt = s_grindCandidates.emplace(candidateKey, std::move(candidates)).first;
        }

        for (const CreatureSpawnInfo* spawn : candidatesIt->second)
        {
            if (!spawn)
                continue;
            const PositionInfo& pos = spawn->position;
            float distSq = Helper::DistanceSq2D(pos.x, pos.y, botX, botY);
            if (distSq >= minDistSq && distSq < bestDistSq)
            {
                bestDistSq = distSq;
                outPos = pos;
                outCreatureEntry = spawn->entry;
                found = true;
            }
        }

        return found;
    }
}
