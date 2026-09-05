#include "Globals/ObjectMgr.h"
#include "NpcFinder.h"
#include "Cache/BotCache.h"
#include "Brain/QuestDistributionPolicy.h"
#include "Player.h"
#include "Creature.h"
#include "UnitDefines.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Helper/MathUtils.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <vector>

namespace Helper
{
    bool FindNpcLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId)
    {
        return Cache::BotCache::FindNpcLocation(entry, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId);
    }

    bool FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId)
    {
        return Cache::BotCache::FindGameObjectLocation(entry, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId);
    }

    namespace
    {
        bool SelectDiversifiedLocation(std::vector<Common::PositionInfo> locations,
            uint32_t entry, uint64_t botKey, uint32_t questId,
            float& outX, float& outY, float& outZ, uint32_t& outMapId,
            float nearX, float nearY, uint32_t nearMapId)
        {
            if (nearMapId != std::numeric_limits<uint32_t>::max())
            {
                locations.erase(std::remove_if(locations.begin(), locations.end(),
                    [nearMapId](const Common::PositionInfo& location) {
                        return location.mapId != nearMapId;
                    }), locations.end());
            }
            if (locations.empty())
                return false;

            auto nearer = [nearX, nearY](const Common::PositionInfo& left,
                    const Common::PositionInfo& right) {
                    float leftDistance = Helper::DistanceSq2D(
                        left.x, left.y, nearX, nearY);
                    float rightDistance = Helper::DistanceSq2D(
                        right.x, right.y, nearX, nearY);
                    if (leftDistance != rightDistance)
                        return leftDistance < rightDistance;
                    if (left.mapId != right.mapId)
                        return left.mapId < right.mapId;
                    if (left.x != right.x)
                        return left.x < right.x;
                    return left.y < right.y;
                };

            constexpr std::size_t NearbyDistributionPool = 8;
            std::size_t poolSize = std::min(
                NearbyDistributionPool, locations.size());
            std::partial_sort(locations.begin(),
                locations.begin() + poolSize, locations.end(), nearer);
            // Rendezvous-style affinity keeps the chosen spawn stable as the
            // bot approaches it, while different bot keys spread a cohort
            // across the same nearby pool.
            std::size_t selectedIndex = 0;
            uint64_t selectedAffinity = 0;
            for (std::size_t index = 0; index < poolSize; ++index)
            {
                const Common::PositionInfo& candidate = locations[index];
                uint64_t affinity = Brain::QuestLocationAffinity(botKey,
                    questId, entry, candidate.mapId,
                    static_cast<int32_t>(std::lround(candidate.x * 2.0f)),
                    static_cast<int32_t>(std::lround(candidate.y * 2.0f)));
                if (index == 0 || affinity > selectedAffinity)
                {
                    selectedAffinity = affinity;
                    selectedIndex = index;
                }
            }
            const Common::PositionInfo& selected = locations[selectedIndex];
            outX = selected.x;
            outY = selected.y;
            outZ = selected.z;
            outMapId = selected.mapId;
            return true;
        }
    }

    bool FindDiversifiedNpcLocation(uint32_t entry, uint64_t botKey,
        uint32_t questId, float& outX, float& outY, float& outZ,
        uint32_t& outMapId, float nearX, float nearY, float /*nearZ*/,
        uint32_t nearMapId)
    {
        return SelectDiversifiedLocation(Cache::BotCache::GetNpcLocations(entry),
            entry, botKey, questId, outX, outY, outZ, outMapId,
            nearX, nearY, nearMapId);
    }

    bool FindDiversifiedGameObjectLocation(uint32_t entry, uint64_t botKey,
        uint32_t questId, float& outX, float& outY, float& outZ,
        uint32_t& outMapId, float nearX, float nearY, float /*nearZ*/,
        uint32_t nearMapId)
    {
        return SelectDiversifiedLocation(
            Cache::BotCache::GetGameObjectLocations(entry), entry, botKey,
            questId, outX, outY, outZ, outMapId, nearX, nearY, nearMapId);
    }

    bool FindDiversifiedLocationCascading(uint32_t entry, bool isGameObject,
        uint64_t botKey, uint32_t questId, Player* bot,
        float& outX, float& outY, float& outZ, uint32_t& outMapId)
    {
        if (!bot)
            return false;

        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float bz = bot->GetPositionZ();
        uint32_t bMap = bot->GetMapId();

        // 1. Diversified lookup on current map
        bool located = isGameObject
            ? FindDiversifiedGameObjectLocation(entry, botKey, questId, outX, outY, outZ, outMapId, bx, by, bz, bMap)
            : FindDiversifiedNpcLocation(entry, botKey, questId, outX, outY, outZ, outMapId, bx, by, bz, bMap);
        if (located) return true;

        // 2. Direct lookup on current map
        located = isGameObject
            ? FindGameObjectLocation(entry, outX, outY, outZ, outMapId, bx, by, bz, bMap)
            : FindNpcLocation(entry, outX, outY, outZ, outMapId, bx, by, bz, bMap);
        if (located) return true;

        // 3. Diversified lookup across all maps
        located = isGameObject
            ? FindDiversifiedGameObjectLocation(entry, botKey, questId, outX, outY, outZ, outMapId, bx, by, bz)
            : FindDiversifiedNpcLocation(entry, botKey, questId, outX, outY, outZ, outMapId, bx, by, bz);
        if (located) return true;

        // 4. Direct lookup across all maps
        return isGameObject
            ? FindGameObjectLocation(entry, outX, outY, outZ, outMapId, bx, by, bz)
            : FindNpcLocation(entry, outX, outY, outZ, outMapId, bx, by, bz);
    }

    std::vector<Creature*> NpcUtils::FindNearbyFriendlyCreatures(Player* bot, float range)
    {
        if (!bot || !bot->IsInWorld())
            return {};

        std::list<Creature*> creatures;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, range);
        Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, range);

        std::vector<Creature*> result;
        result.reserve(creatures.size());
        for (Creature* creature : creatures)
        {
            if (creature && creature->IsAlive() && !creature->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE) &&
                !creature->GetCharmerGUID() && creature->GetReactionTo(bot) > REP_UNFRIENDLY)
                result.push_back(creature);
        }
        return result;
    }

    Creature* NpcUtils::FindNearbyCreatureByEntry(Player* bot, uint32_t entry, float range)
    {
        if (!bot) return nullptr;
        Creature* nearest = nullptr;
        float nearestDistSq = std::numeric_limits<float>::max();
        for (Creature* creature : FindNearbyFriendlyCreatures(bot, range))
        {
            if (entry == 0 || creature->GetEntry() == entry)
            {
                float distSq = bot->GetExactDistSq(creature);
                if (distSq < nearestDistSq)
                {
                    nearestDistSq = distSq;
                    nearest = creature;
                }
            }
        }
        return nearest;
    }

    Creature* NpcUtils::FindNearbyCreatureByEntryAnyReaction(Player* bot,
        uint32_t entry, float range)
    {
        if (!bot || !bot->IsInWorld())
            return nullptr;

        std::list<Creature*> creatures;
        Trinity::AnyUnitInObjectRangeCheck check(bot, range);
        Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck>
            searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, range);

        Creature* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (Creature* creature : creatures)
        {
            if (!creature || !creature->IsAlive() ||
                (entry != 0 && creature->GetEntry() != entry))
            {
                continue;
            }

            float distance = bot->GetDistance(creature);
            if (distance < nearestDistance)
            {
                nearest = creature;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    Creature* NpcUtils::FindNearbyServiceNpc(Player* bot, bool requireVendor, bool requireRepair,
        float range, std::function<bool(Creature*)> capability)
    {
        if (!bot || !bot->IsInWorld() || (!requireVendor && !requireRepair))
            return nullptr;

        Creature* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (Creature* creature : FindNearbyFriendlyCreatures(bot, range))
        {
            if (creature->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE) || creature->GetCharmerGUID())
                continue;
            if (requireVendor && !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
                continue;
            if (requireRepair && !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
                continue;
            if (capability && !capability(creature))
                continue;

            float distance = bot->GetDistance(creature);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = creature;
            }
        }
        return nearest;
    }

    bool NpcUtils::IsInInteractionRange(Player* bot, float targetX, float targetY, float targetZ, float maxDistance)
    {
        if (!bot) return false;

        return Helper::IsIn3DRange(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), targetX, targetY, targetZ, maxDistance);
    }

    InteractionStatus NpcUtils::GetInteractionStatus(Player* bot, WorldObject* target, float maxDistance)
    {
        if (!bot || !bot->IsInWorld() || !target || !target->IsInWorld() || target->GetMap() != bot->GetMap())
            return InteractionStatus::Invalid;

        if (Creature* creature = target->ToCreature())
        {
            if (!creature->IsAlive() || creature->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE) ||
                creature->GetCharmerGUID() || creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
            {
                return InteractionStatus::Invalid;
            }
        }

        float effectiveMaxDistance = maxDistance;
        if (GameObject* go = target->ToGameObject())
        {
            float collisionRadius = go->GetGOInfo() ? go->GetGOInfo()->size : 1.0f;
            effectiveMaxDistance += collisionRadius;
        }

        float dist = bot->GetDistance(target);
        if (dist > effectiveMaxDistance)
            return InteractionStatus::NeedsMovement;

        // When in close interaction range (<= 2.5 yards) and on the same vertical level,
        // tolerate minor line-of-sight occlusions (such as shop counters, low fences, or raycast flaws).
        bool hasLOS = bot->IsWithinLOSInMap(target);
        if (!hasLOS && (dist > 2.5f || std::fabs(bot->GetPositionZ() - target->GetPositionZ()) >= 2.0f))
            return InteractionStatus::NeedsMovement;

        return InteractionStatus::Ready;
    }

    void NpcUtils::PrepareCreatureInteraction(Player* bot, Creature* creature)
    {
        if (!bot || !creature)
            return;

        bot->SetSelection(creature->GetGUID());
        bot->PrepareQuestMenu(creature->GetGUID());
        bot->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
    }
}
