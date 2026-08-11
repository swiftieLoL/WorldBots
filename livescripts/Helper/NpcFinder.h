#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include "Constants.h"

class Creature;
class Player;
class WorldObject;

namespace Helper
{
    enum class InteractionStatus : uint8_t
    {
        Ready,
        NeedsMovement,
        Invalid
    };

    bool FindNpcLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
    bool FindNpcLocationByName(const char* name, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f);
    bool FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());

    class NpcUtils
    {
    public:
        static Creature* FindNearbyCreatureByEntry(Player* bot, uint32_t entry, float range = Constants::DefaultNpcSearchRadius);
        static Creature* FindNearbyServiceNpc(Player* bot, bool requireVendor, bool requireRepair,
            float range = Constants::DefaultNpcSearchRadius);
        static bool IsInInteractionRange(Player* bot, float targetX, float targetY, float targetZ, float maxDistance = Constants::QuestInteractionRange);
        static InteractionStatus GetInteractionStatus(Player* bot, WorldObject* target, float maxDistance = Constants::QuestInteractionRange);
        static void PrepareCreatureInteraction(Player* bot, Creature* creature);
        static bool InteractWithNpc(Player* bot, uint32_t npcEntry, float range, std::function<void(Creature*)> interactionPayload);
    };
}
