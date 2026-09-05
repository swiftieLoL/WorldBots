#include "Globals/ObjectMgr.h"
#include "QuestItemSourceResolver.h"
#include "QuestTargetResolver.h"
#include "Cache/BotCache.h"
#include "Helper/MathUtils.h"
#include "Helper/LootSourcePolicy.h"
#include "Helper/NpcFinder.h"
#include "Helper/ProgressionPolicy.h"
#include "Config/BotConfig.h"
#include "Diagnostics/BotTrace.h"
#include "Log.h"
#include "Player.h"
#include "QuestDef.h"
#include <cmath>
#include <limits>

namespace Sense
{
    Blackboard::QuestObjectiveData QuestItemSourceResolver::Resolve(Player* bot, Quest const* questTemplate, Blackboard::QuestState& questState,
        uint32_t questId, uint32_t itemId, uint32_t requiredCount, bool forceFullRescan)
    {
        Blackboard::QuestObjectiveData objective;
        objective.type = Blackboard::QuestObjectiveType::CollectItem;
        objective.itemId = itemId;
        objective.requiredCount = requiredCount;
        objective.currentCount = bot->GetItemCount(itemId, false);

        uint64_t cacheKey = QuestTargetResolver::ItemSourceCacheKey(questId, itemId);
        auto cacheIt = questState.itemSourceCache.find(cacheKey);
        bool isQuestSourceItem = itemId == questTemplate->GetSrcItemId();
        bool useCache = (!forceFullRescan || isQuestSourceItem) &&
            cacheIt != questState.itemSourceCache.end() &&
            cacheIt->second.lastKnownCount == objective.currentCount &&
            cacheIt->second.sourceResolutionAttempted;

        if (useCache)
        {
            objective.targetEntry = cacheIt->second.resolvedEntry;
            objective.targetKind = cacheIt->second.targetKind;
            objective.vendorPurchase = cacheIt->second.vendorPurchase;
            if (cacheIt->second.hasLocation)
            {
                objective.location = { cacheIt->second.x, cacheIt->second.y,
                    cacheIt->second.z, cacheIt->second.mapId };
                objective.hasLocation = true;
            }
            return objective;
        }

        // Completed objectives still belong in the blackboard so completion
        // checks remain accurate, but they do not need source discovery.
        if (objective.currentCount >= objective.requiredCount)
        {
            Blackboard::CachedItemSource& cached = questState.itemSourceCache[cacheKey];
            cached = {};
            cached.lastKnownCount = objective.currentCount;
            cached.sourceResolutionAttempted = true;
            return objective;
        }

        ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemId);
        std::string itemName = itemProto ? itemProto->Name1 : "Unknown Item";
        bool trace = Diagnostics::BotTrace::ShouldLog(bot);
        if (trace)
        {
            TC_LOG_INFO("server", "[WorldBots] [Quest] ========================================================");
            TC_LOG_INFO("server", "[WorldBots] [Quest] Bot '{}' [Item Objective Step 1] Quest {} ('{}') Requires Item {} ('{}') Progress: {}/{}",
                bot->GetName(), questId, questTemplate->GetTitle(), itemId, itemName, objective.currentCount, requiredCount);
        }

        if (isQuestSourceItem)
        {
            if (trace)
            {
                TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective] Item {} ('{}') is the quest source item; source-item recovery will handle inventory delivery (no loot source lookup)",
                    itemId, itemName);
            }
            Blackboard::CachedItemSource& cached = questState.itemSourceCache[cacheKey];
            cached.resolvedEntry = 0;
            cached.x = cached.y = cached.z = 0.0f;
            cached.mapId = bot->GetMapId();
            cached.lastKnownCount = objective.currentCount;
            cached.sourceResolutionAttempted = true;
            cached.targetKind = Blackboard::QuestTargetKind::None;
            cached.hasLocation = false;
            cached.vendorPurchase = false;
            return objective;
        }

        auto sources = Cache::BotCache::GetItemLootSources(itemId);
        auto vendorSources = Cache::BotCache::GetItemVendorSources(itemId);
        if (trace)
        {
            TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective Step 2] Item {} ('{}') has {} registered vendor source(s) and {} loot source(s) in BotCache",
                itemId, itemName, vendorSources.size(), sources.size());
        }

        float bestDistanceSq = std::numeric_limits<float>::max();
        float bestScore = std::numeric_limits<float>::max();
        float bestDropChance = 0.0f;
        uint32_t bestEntry = 0;
        Blackboard::QuestTargetKind bestKind = Blackboard::QuestTargetKind::None;
        Blackboard::PositionInfo bestPosition{ 0.0f, 0.0f, 0.0f, bot->GetMapId() };
        std::string bestName = "None";
        bool vendorPurchase = false;
        std::vector<Cache::LootSource> eligibleSources;

        // A stationary vendor is a deterministic 100% source, but still pays
        // its travel cost. This supports purchase-only objectives such as Dry
        // Times without forcing a continent-scale vendor detour when a strong
        // local loot source is available.
        for (const Cache::VendorInfo& vendor : vendorSources)
        {
            float distanceSq = Helper::DistanceSq2D(vendor.x, vendor.y,
                bot->GetPositionX(), bot->GetPositionY());
            float score = Helper::ScoreLootSource(distanceSq, 100.0f,
                vendor.mapId == bot->GetMapId());
            if (score < bestScore)
            {
                bestScore = score;
                bestDistanceSq = distanceSq;
                bestEntry = vendor.entry;
                bestKind = Blackboard::QuestTargetKind::Creature;
                bestPosition = { vendor.x, vendor.y, vendor.z, vendor.mapId };
                if (CreatureTemplate const* vendorInfo = sObjectMgr->GetCreatureTemplate(vendor.entry))
                    bestName = vendorInfo->Name;
                vendorPurchase = true;
            }
        }

        for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
        {
            const Cache::LootSource& source = sources[sourceIndex];
            bool isGameObject = source.type == Cache::LootSourceType::GameObject;
            CreatureTemplate const* creatureInfo = !isGameObject ? sObjectMgr->GetCreatureTemplate(source.entry) : nullptr;
            GameObjectTemplate const* gameObjectInfo = isGameObject ? sObjectMgr->GetGameObjectTemplate(source.entry) : nullptr;
            std::string sourceName = creatureInfo ? creatureInfo->Name : (gameObjectInfo ? gameObjectInfo->name : "Unknown Entity");

            if (creatureInfo && !Helper::IsQuestObjectiveCreatureSuitable(
                bot->GetLevel(), creatureInfo->maxlevel,
                Config::BotConfig::GetQuestMaxLevelsAboveBot()))
            {
                continue;
            }
            eligibleSources.push_back(source);

            float x = 0.0f, y = 0.0f, z = 0.0f;
            uint32_t mapId = 0;
            if (!Helper::FindDiversifiedLocationCascading(source.entry, isGameObject,
                static_cast<uint64_t>(bot->GetGUID().GetCounter()), questId, bot, x, y, z, mapId))
                continue;

            float distanceSq = Helper::DistanceSq2D(x, y, bot->GetPositionX(), bot->GetPositionY());
            float score = Helper::ScoreLootSource(distanceSq, source.dropChance,
                mapId == bot->GetMapId());
            if (score < bestScore)
            {
                bestScore = score;
                bestDistanceSq = distanceSq;
                bestDropChance = source.dropChance;
                bestEntry = source.entry;
                bestKind = isGameObject ? Blackboard::QuestTargetKind::GameObject : Blackboard::QuestTargetKind::Creature;
                bestPosition = { x, y, z, mapId };
                bestName = std::move(sourceName);
                vendorPurchase = false;
            }
        }

        if (bestEntry == 0 && !eligibleSources.empty())
        {
            bestEntry = eligibleSources.front().entry;
            bestKind = eligibleSources.front().type == Cache::LootSourceType::GameObject
                ? Blackboard::QuestTargetKind::GameObject : Blackboard::QuestTargetKind::Creature;
            if (trace)
            {
                TC_LOG_INFO("server", "[WorldBots] [Quest] [Step 4: Fallback] No spawn found on map {}; defaulting to first source Entry {}",
                    bot->GetMapId(), bestEntry);
            }
        }

        if (bestEntry != 0)
        {
            if (trace)
            {
                if (vendorPurchase)
                    TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective Step 5: RESULT] For Item {} ('{}'), selected vendor Entry {} ('{}') at ({:.1f}, {:.1f}, {:.1f}) Map {}",
                        itemId, itemName, bestEntry, bestName, bestPosition.x, bestPosition.y, bestPosition.z, bestPosition.mapId);
                else
                    TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective Step 5: RESULT] For Item {} ('{}'), selected loot Entry {} ('{}') at ({:.1f}, {:.1f}, {:.1f}) Map {} (authored chance: {:.2f}%)",
                        itemId, itemName, bestEntry, bestName, bestPosition.x, bestPosition.y, bestPosition.z, bestPosition.mapId, bestDropChance);
            }
        }
        else
        {
            if (trace)
            {
                TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective Step 5: RESULT] WARNING: No valid source entry found for Item {} ('{}')!", itemId, itemName);
            }
        }
        if (trace)
            TC_LOG_INFO("server", "[WorldBots] [Quest] ========================================================");

        Blackboard::CachedItemSource& cached = questState.itemSourceCache[cacheKey];
        cached.resolvedEntry = bestEntry;
        cached.x = bestPosition.x;
        cached.y = bestPosition.y;
        cached.z = bestPosition.z;
        cached.mapId = bestPosition.mapId;
        cached.lastKnownCount = objective.currentCount;
        cached.sourceResolutionAttempted = true;
        cached.targetKind = bestKind;
        cached.hasLocation = bestDistanceSq != std::numeric_limits<float>::max();
        cached.vendorPurchase = vendorPurchase;

        objective.targetEntry = bestEntry;
        objective.targetKind = bestKind;
        objective.vendorPurchase = vendorPurchase;
        if (cached.hasLocation)
        {
            objective.location = bestPosition;
            objective.hasLocation = true;
        }
        return objective;
    }
}
