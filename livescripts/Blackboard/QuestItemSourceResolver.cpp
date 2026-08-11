#include "Globals/ObjectMgr.h"
#include "QuestItemSourceResolver.h"
#include "QuestTargetResolver.h"
#include "Cache/BotCache.h"
#include "Helper/MathUtils.h"
#include "Helper/NpcFinder.h"
#include "Diagnostics/BotTrace.h"
#include "Log.h"
#include "Player.h"
#include "QuestDef.h"
#include <cmath>
#include <limits>

namespace Blackboard
{
    QuestObjectiveData QuestItemSourceResolver::Resolve(Player* bot, Quest const* questTemplate, QuestState& questState,
        uint32_t questId, uint32_t itemId, uint32_t requiredCount, bool forceFullRescan)
    {
        QuestObjectiveData objective;
        objective.type = QuestObjectiveType::CollectItem;
        objective.itemId = itemId;
        objective.requiredCount = requiredCount;
        objective.currentCount = bot->GetItemCount(itemId, false);

        uint64_t cacheKey = QuestTargetResolver::ItemSourceCacheKey(questId, itemId);
        auto cacheIt = questState.itemSourceCache.find(cacheKey);
        bool isQuestSourceItem = itemId == questTemplate->GetSrcItemId();
        bool useCache = (!forceFullRescan || isQuestSourceItem) &&
            cacheIt != questState.itemSourceCache.end() &&
            cacheIt->second.lastKnownCount == objective.currentCount &&
            cacheIt->second.sourceResolutionAttempted &&
            (!cacheIt->second.hasLocation || cacheIt->second.mapId == bot->GetMapId());

        if (useCache)
        {
            objective.targetEntry = cacheIt->second.resolvedEntry;
            objective.targetKind = cacheIt->second.targetKind;
            if (cacheIt->second.hasLocation)
            {
                objective.location = { cacheIt->second.x, cacheIt->second.y,
                    cacheIt->second.z, cacheIt->second.mapId };
                objective.hasLocation = true;
            }
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
            CachedItemSource& cached = questState.itemSourceCache[cacheKey];
            cached.resolvedEntry = 0;
            cached.x = cached.y = cached.z = 0.0f;
            cached.mapId = bot->GetMapId();
            cached.lastKnownCount = objective.currentCount;
            cached.sourceResolutionAttempted = true;
            cached.targetKind = QuestTargetKind::None;
            cached.hasLocation = false;
            return objective;
        }

        auto sources = Cache::BotCache::GetItemLootSources(itemId);
        if (trace)
        {
            TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective Step 2] Item {} ('{}') has {} registered loot source entries in BotCache",
                itemId, itemName, sources.size());
        }

        float bestDistanceSq = std::numeric_limits<float>::max();
        uint32_t bestEntry = 0;
        QuestTargetKind bestKind = QuestTargetKind::None;
        PositionInfo bestPosition{ 0.0f, 0.0f, 0.0f, bot->GetMapId() };
        std::string bestName = "None";

        for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
        {
            const Cache::LootSource& source = sources[sourceIndex];
            bool isGameObject = source.type == Cache::LootSourceType::GameObject;
            CreatureTemplate const* creatureInfo = !isGameObject ? sObjectMgr->GetCreatureTemplate(source.entry) : nullptr;
            GameObjectTemplate const* gameObjectInfo = isGameObject ? sObjectMgr->GetGameObjectTemplate(source.entry) : nullptr;
            std::string sourceName = creatureInfo ? creatureInfo->Name : (gameObjectInfo ? gameObjectInfo->name : "Unknown Entity");

            float x = 0.0f, y = 0.0f, z = 0.0f;
            uint32_t mapId = 0;
            bool located = isGameObject
                ? Helper::FindGameObjectLocation(source.entry, x, y, z, mapId,
                    bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId())
                : Helper::FindNpcLocation(source.entry, x, y, z, mapId,
                    bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
            if (!located || mapId != bot->GetMapId())
                continue;

            float distanceSq = Helper::DistanceSq2D(x, y, bot->GetPositionX(), bot->GetPositionY());
            if (distanceSq < bestDistanceSq)
            {
                bestDistanceSq = distanceSq;
                bestEntry = source.entry;
                bestKind = isGameObject ? QuestTargetKind::GameObject : QuestTargetKind::Creature;
                bestPosition = { x, y, z, mapId };
                bestName = std::move(sourceName);
            }
        }

        if (bestEntry == 0 && !sources.empty())
        {
            bestEntry = sources.front().entry;
            bestKind = sources.front().type == Cache::LootSourceType::GameObject
                ? QuestTargetKind::GameObject : QuestTargetKind::Creature;
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
                TC_LOG_INFO("server", "[WorldBots] [Quest] [Item Objective Step 5: RESULT] For Item {} ('{}'), Selected Entry {} ('{}') at ({:.1f}, {:.1f}, {:.1f}) Map {}",
                    itemId, itemName, bestEntry, bestName, bestPosition.x, bestPosition.y, bestPosition.z, bestPosition.mapId);
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

        CachedItemSource& cached = questState.itemSourceCache[cacheKey];
        cached.resolvedEntry = bestEntry;
        cached.x = bestPosition.x;
        cached.y = bestPosition.y;
        cached.z = bestPosition.z;
        cached.mapId = bestPosition.mapId;
        cached.lastKnownCount = objective.currentCount;
        cached.sourceResolutionAttempted = true;
        cached.targetKind = bestKind;
        cached.hasLocation = bestDistanceSq != std::numeric_limits<float>::max();

        objective.targetEntry = bestEntry;
        objective.targetKind = bestKind;
        if (cached.hasLocation)
        {
            objective.location = bestPosition;
            objective.hasLocation = true;
        }
        return objective;
    }
}
