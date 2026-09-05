#include "SenseUpdaters.h"
#include "Pet.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Unit.h"

namespace Sense
{
    bool CombatSenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        return Detail::ServiceSubstate(bb.combat, deltaMs,
            [&]() { Refresh(bot, bb.combat); });
    }

    void CombatSenseUpdater::Refresh(Player* bot, Blackboard::CombatState& combat)
    {
        combat.attackerGuids.clear();
        combat.primaryAttackerGuid.Clear();

        if (Unit* victim = bot->GetVictim())
        {
            if (victim->IsAlive())
            {
                combat.currentTargetGuid = victim->GetGUID();
            }
            else
            {
                combat.currentTargetGuid.Clear();
            }
        }
        else
        {
            combat.currentTargetGuid.Clear();
        }

        const auto& attackers = bot->getAttackers();
        float minAttackerDist = 99999.0f;

        for (Unit* attacker : attackers)
        {
            if (attacker && attacker->IsInWorld() && attacker->IsAlive() && attacker->GetMap() == bot->GetMap())
            {
                combat.attackerGuids.push_back(attacker->GetGUID());
                float dist = bot->GetDistance(attacker);
                if (dist < minAttackerDist)
                {
                    minAttackerDist = dist;
                    combat.primaryAttackerGuid = attacker->GetGUID();
                }
            }
        }

        if (Pet* pet = bot->GetPet())
        {
            if (pet->IsInWorld() && pet->IsAlive() && pet->GetMap() == bot->GetMap())
            {
                const auto& petAttackers = pet->getAttackers();
                for (Unit* attacker : petAttackers)
                {
                    if (attacker && attacker->IsInWorld() && attacker->IsAlive() && attacker->GetMap() == bot->GetMap())
                    {
                        ObjectGuid guid = attacker->GetGUID();
                        if (std::find(combat.attackerGuids.begin(), combat.attackerGuids.end(), guid) == combat.attackerGuids.end())
                        {
                            combat.attackerGuids.push_back(guid);
                            float dist = bot->GetDistance(attacker);
                            if (dist < minAttackerDist)
                            {
                                minAttackerDist = dist;
                                combat.primaryAttackerGuid = guid;
                            }
                        }
                    }
                }
            }
        }
    }
}

