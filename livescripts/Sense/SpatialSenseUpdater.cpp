#include "SenseUpdaters.h"
#include "Helper/Constants.h"
#include "Globals/ObjectMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "Unit.h"
#include <list>

namespace Sense
{
    bool SpatialSenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        bool wasInitialized = bb.spatial.initialized;
        bool refreshed = Detail::ServiceSubstate(bb.spatial, deltaMs,
            [&]() { Refresh(bot, bb.spatial); });

        // Every bot needs an immediate first snapshot, but keeping every
        // subsequent 200 ms scan on the same scheduler pass creates a large
        // periodic grid-query spike. Seed a stable per-bot phase after that
        // first refresh so the same amount of sensing work is spread out.
        if (refreshed && !wasInitialized && bot &&
            bb.spatial.refreshIntervalMs != 0)
        {
            bb.spatial.elapsedMs = static_cast<uint32_t>(
                bot->GetGUID().GetCounter() % bb.spatial.refreshIntervalMs);
        }
        return refreshed;
    }

    void SpatialSenseUpdater::Refresh(Player* bot, Blackboard::SpatialState& spatial)
    {
        spatial.hostileGuids.clear();
        spatial.nearestEnemyGuid.Clear();
        spatial.nearestFriendlyGuid.Clear();

        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        {
            return;
        }

        std::list<Unit*> units;
        Trinity::AnyUnitInObjectRangeCheck check(bot, Constants::TacticalScanRadius);
        Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
        Cell::VisitGridObjects(bot, searcher, Constants::TacticalScanRadius);

        float minEnemyDist = 99999.0f;
        float minFriendlyDist = 99999.0f;

        for (Unit* u : units)
        {
            if (!u || u == bot || !u->IsAlive() || !u->IsInWorld() || u->GetMap() != bot->GetMap())
                continue;

            float dist = bot->GetDistance(u);

            bool isHostile = u->IsHostileTo(bot) || u->GetVictim() == bot || u->IsInCombatWith(bot);
            if (!isHostile && !bot->IsFriendlyTo(u))
            {
                if (u->IsCreature() && !u->IsCritter() && u->isTargetableForAttack())
                {
                    Creature* c = static_cast<Creature*>(u);
                    if (!c->IsCivilian())
                    {
                        isHostile = true;
                    }
                }
            }

            if (isHostile)
            {
                spatial.hostileGuids.push_back(u->GetGUID());
                if (dist < minEnemyDist)
                {
                    minEnemyDist = dist;
                    spatial.nearestEnemyGuid = u->GetGUID();
                }
            }
            else if (bot->IsFriendlyTo(u))
            {
                if (dist < minFriendlyDist)
                {
                    minFriendlyDist = dist;
                    spatial.nearestFriendlyGuid = u->GetGUID();
                }
            }
        }
    }

}
