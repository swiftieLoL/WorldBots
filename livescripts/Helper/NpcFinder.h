#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>
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
    bool FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
    bool FindDiversifiedNpcLocation(uint32_t entry, uint64_t botKey,
        uint32_t questId, float& outX, float& outY, float& outZ,
        uint32_t& outMapId, float nearX, float nearY, float nearZ,
        uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
    bool FindDiversifiedGameObjectLocation(uint32_t entry, uint64_t botKey,
        uint32_t questId, float& outX, float& outY, float& outZ,
        uint32_t& outMapId, float nearX, float nearY, float nearZ,
        uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
    bool FindDiversifiedLocationCascading(uint32_t entry, bool isGameObject,
        uint64_t botKey, uint32_t questId, Player* bot,
        float& outX, float& outY, float& outZ, uint32_t& outMapId);

    class NpcUtils
    {
    public:
        static std::vector<Creature*> FindNearbyFriendlyCreatures(Player* bot,
            float range = Constants::DefaultNpcSearchRadius);
        static Creature* FindNearbyCreatureByEntry(Player* bot, uint32_t entry, float range = Constants::DefaultNpcSearchRadius);
        // Spell-credit targets may legitimately be hostile or neutral. Keep
        // this separate from NPC interaction lookups, which must stay friendly.
        static Creature* FindNearbyCreatureByEntryAnyReaction(Player* bot,
            uint32_t entry, float range = Constants::DefaultNpcSearchRadius);
        static Creature* FindNearbyServiceNpc(Player* bot, bool requireVendor, bool requireRepair,
            float range = Constants::DefaultNpcSearchRadius,
            std::function<bool(Creature*)> capability = {});
        static bool IsInInteractionRange(Player* bot, float targetX, float targetY, float targetZ, float maxDistance = Constants::QuestInteractionRange);
        static InteractionStatus GetInteractionStatus(Player* bot, WorldObject* target, float maxDistance = Constants::QuestInteractionRange);
        static void PrepareCreatureInteraction(Player* bot, Creature* creature);
    };
}
