#include "Globals/ObjectMgr.h"
#include "NpcFinder.h"
#include "Cache/BotCache.h"
#include "Player.h"
#include "Creature.h"
#include "UnitDefines.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Helper/MathUtils.h"

namespace Helper
{
    bool FindNpcLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId)
    {
        return Cache::BotCache::FindNpcLocation(entry, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId);
    }

    bool FindNpcLocationByName(const char* name, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ)
    {
        return Cache::BotCache::FindNpcLocationByName(name, outX, outY, outZ, outMapId, nearX, nearY, nearZ);
    }

    bool FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX, float nearY, float nearZ, uint32_t nearMapId)
    {
        return Cache::BotCache::FindGameObjectLocation(entry, outX, outY, outZ, outMapId, nearX, nearY, nearZ, nearMapId);
    }

    Creature* NpcUtils::FindNearbyCreatureByEntry(Player* bot, uint32_t entry, float range)
    {
        if (!bot || !bot->IsInWorld()) return nullptr;

        std::list<Creature*> creatures;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, range);
        Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, range);

        for (Creature* creature : creatures)
        {
            if (creature && (creature->GetEntry() == entry || entry == 0))
            {
                return creature;
            }
        }
        return nullptr;
    }

    Creature* NpcUtils::FindNearbyServiceNpc(Player* bot, bool requireVendor, bool requireRepair, float range)
    {
        if (!bot || !bot->IsInWorld() || (!requireVendor && !requireRepair))
            return nullptr;

        std::list<Creature*> creatures;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(bot, bot, range);
        Trinity::CreatureListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, creatures, check);
        Cell::VisitGridObjects(bot, searcher, range);

        Creature* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (Creature* creature : creatures)
        {
            if (!creature || !creature->IsAlive() || creature->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE) ||
                creature->GetCharmerGUID() || creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
                continue;
            if (requireVendor && !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
                continue;
            if (requireRepair && !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
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

        if (bot->GetDistance(target) > maxDistance || !bot->IsWithinLOSInMap(target))
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

    bool NpcUtils::InteractWithNpc(Player* bot, uint32_t npcEntry, float range, std::function<void(Creature*)> interactionPayload)
    {
        if (!bot || !bot->IsInWorld()) return false;
        Creature* creature = FindNearbyCreatureByEntry(bot, npcEntry, range);
        if (!creature || !creature->IsAlive()) return false;

        if (GetInteractionStatus(bot, creature, Constants::QuestInteractionRange) != InteractionStatus::Ready)
            return false;

        PrepareCreatureInteraction(bot, creature);

        if (interactionPayload)
        {
            interactionPayload(creature);
        }
        return true;
    }
}
