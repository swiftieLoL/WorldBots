#pragma once

#include "BotBlackboard.h"
#include "Player.h"
#include "Helper/MovementManager.h"

namespace Blackboard
{
    class BlackboardUpdater
    {
    public:
        static void UpdateAll(Player* bot, MovementManager* movement, BotBlackboard& bb, uint32_t deltaMs);
        static void UpdateSelf(Player* bot, SelfState& self);
        static void UpdateSpatial(Player* bot, SpatialState& spatial);
        static void UpdateParty(Player* bot, PartyState& party);
        static void UpdateCombat(Player* bot, CombatState& combat);
        static void UpdateNavigation(Player* bot, MovementManager* movement, NavigationState& nav);
        static void UpdateInventory(Player* bot, InventoryState& inv);
        static void UpdateQuest(Player* bot, QuestState& quest);
    };
}
