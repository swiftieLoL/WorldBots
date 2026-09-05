#include "Globals/ObjectMgr.h"
#include "QuestTargetResolver.h"
#include "Cache/BotCache.h"
#include "DataStores/DBCStores.h"
#include "Helper/MathUtils.h"
#include "Helper/NpcFinder.h"
#include "Map.h"
#include "Player.h"
#include "QuestDef.h"
#include <algorithm>
#include <limits>

namespace Sense
{
    uint64_t QuestTargetResolver::ItemSourceCacheKey(uint32_t questId, uint32_t itemId)
    {
        return (static_cast<uint64_t>(questId) << 32) | itemId;
    }

    float QuestTargetResolver::ResolveExplorationHeight(Player* bot, uint32_t mapId, float x, float y, float fallbackZ)
    {
        if (!bot || !bot->GetMap() || mapId != bot->GetMapId())
            return fallbackZ;

        float z = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, fallbackZ, true);
        return z > -50000.0f && z < 50000.0f ? z : fallbackZ;
    }

    bool QuestTargetResolver::ResolveExplorationTarget(Player* bot, Quest const* quest, Blackboard::PositionInfo& position, float& radius)
    {
        if (!bot || !quest || !sObjectMgr)
            return false;

        AreaTriggerEntry const* anyTrigger = nullptr;
        AreaTriggerEntry const* sameMapTrigger = nullptr;
        for (AreaTriggerEntry const* trigger : sAreaTriggerStore)
        {
            if (!trigger || sObjectMgr->GetQuestForAreaTrigger(trigger->ID) != quest->GetQuestId())
                continue;

            anyTrigger = trigger;
            if (trigger->ContinentID == bot->GetMapId())
            {
                sameMapTrigger = trigger;
                break;
            }
        }

        if (AreaTriggerEntry const* trigger = sameMapTrigger ? sameMapTrigger : anyTrigger)
        {
            position = { trigger->Pos.X, trigger->Pos.Y, trigger->Pos.Z, trigger->ContinentID };
            radius = trigger->Radius;
            if (radius <= 0.0f)
                radius = std::max({ trigger->BoxLength, trigger->BoxWidth, trigger->BoxHeight }) * 0.5f;
            if (radius <= 0.0f)
                radius = 8.0f;
            return true;
        }

        float poiX = quest->GetPOIx();
        float poiY = quest->GetPOIy();
        uint32 poiMap = quest->GetPOIContinent();
        if (poiX != 0.0f || poiY != 0.0f)
        {
            position = { poiX, poiY, ResolveExplorationHeight(bot, poiMap, poiX, poiY, bot->GetPositionZ()), poiMap };
            radius = 12.0f;
            return true;
        }

        if (QuestPOIWrapper const* wrapper = sObjectMgr->GetQuestPOIWrapper(quest->GetQuestId()))
        {
            QuestPOIBlobData const* anyBlob = nullptr;
            QuestPOIBlobData const* sameMapBlob = nullptr;
            for (const QuestPOIBlobData& blob : wrapper->POIData.QuestPOIBlobDataStats)
            {
                if (blob.QuestPOIBlobPointStats.empty())
                    continue;
                anyBlob = &blob;
                if (blob.MapID == bot->GetMapId())
                {
                    sameMapBlob = &blob;
                    break;
                }
            }

            if (QuestPOIBlobData const* blob = sameMapBlob ? sameMapBlob : anyBlob)
            {
                double totalX = 0.0;
                double totalY = 0.0;
                for (const QuestPOIBlobPoint& point : blob->QuestPOIBlobPointStats)
                {
                    totalX += point.X;
                    totalY += point.Y;
                }

                float x = static_cast<float>(totalX / blob->QuestPOIBlobPointStats.size());
                float y = static_cast<float>(totalY / blob->QuestPOIBlobPointStats.size());
                position = { x, y, ResolveExplorationHeight(bot, blob->MapID, x, y, bot->GetPositionZ()), blob->MapID };
                radius = 12.0f;
                return true;
            }
        }

        return false;
    }

    bool QuestTargetResolver::ResolveNearestQuestEnder(Player* bot, uint32_t questId, uint32_t& entry,
        Blackboard::QuestTargetKind& kind, Blackboard::PositionInfo& position)
    {
        if (!bot)
            return false;

        float bestDistanceSq = std::numeric_limits<float>::max();
        bool found = false;
        auto consider = [&](uint32_t candidateEntry, Blackboard::QuestTargetKind candidateKind) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            uint32_t mapId = 0;
            bool isGameObject = (candidateKind == Blackboard::QuestTargetKind::GameObject);
            bool located = isGameObject
                ? (Helper::FindGameObjectLocation(candidateEntry, x, y, z, mapId, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId()) ||
                   Helper::FindGameObjectLocation(candidateEntry, x, y, z, mapId, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
                : (Helper::FindNpcLocation(candidateEntry, x, y, z, mapId, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId()) ||
                   Helper::FindNpcLocation(candidateEntry, x, y, z, mapId, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));
            if (!located)
                return;
            float distanceSq = Helper::DistanceSq2D(x, y, bot->GetPositionX(), bot->GetPositionY());
            if (mapId != bot->GetMapId())
                distanceSq += std::numeric_limits<float>::max() / 4.0f;
            if (distanceSq < bestDistanceSq)
            {
                bestDistanceSq = distanceSq;
                entry = candidateEntry;
                kind = candidateKind;
                position = { x, y, z, mapId };
                found = true;
            }
        };

        for (uint32_t candidate : Cache::BotCache::GetQuestEnders(questId))
            consider(candidate, Blackboard::QuestTargetKind::Creature);
        for (uint32_t candidate : Cache::BotCache::GetGameObjectQuestEnders(questId))
            consider(candidate, Blackboard::QuestTargetKind::GameObject);
        return found;
    }
}
