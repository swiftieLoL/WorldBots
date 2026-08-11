#pragma once

#include "Helper/CommonTypes.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <limits>

class Player;

namespace Cache
{
    using PositionInfo = Common::PositionInfo;

    struct VendorInfo
    {
        uint32_t entry = 0;
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        bool isVendor = false;
        bool isRepairer = false;
    };

    enum class LootSourceType
    {
        Creature,
        GameObject
    };

    struct LootSource
    {
        LootSourceType type = LootSourceType::Creature;
        uint32_t entry = 0;

        bool operator==(const LootSource& other) const
        {
            return type == other.type && entry == other.entry;
        }
    };

    class BotCache
    {
    public:
        static void Initialize();
        static bool IsInitialized();

        static bool FindNpcLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
        static bool FindNpcLocationByName(const char* name, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f);
        static bool FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
        static std::vector<PositionInfo> GetGameObjectLocations(uint32_t entry);
        static bool FindNearestVendor(uint32_t mapId, float botX, float botY, float botZ,
            bool requireVendor, bool requireRepair, uint32_t& outVendorEntry, PositionInfo& outPos);
        static bool SuppressVendorLocation(uint32_t entry, uint32_t mapId, float x, float y, float z);

        static std::vector<uint32_t> GetQuestEnders(uint32_t questId);
        static std::vector<uint32_t> GetQuestStarters(uint32_t creatureEntry);
        static std::vector<uint32_t> GetGameObjectQuestEnders(uint32_t questId);
        static std::vector<uint32_t> GetGameObjectQuestStarters(uint32_t gameObjectEntry);
        static uint32_t GetItemLootSource(uint32_t itemId);
        static std::vector<LootSource> GetItemLootSources(uint32_t itemId);
        static bool IsCastCreditQuest(uint32_t questId);

        static bool FindNearestAvailableQuestStarter(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
            PositionInfo& outPos, uint32_t& outQuestId, uint32_t& outEntry, bool& outIsGameObject,
            const std::unordered_set<uint32_t>& excludedQuestIds = {});
        static bool FindNearestSettlementOrVendor(uint32_t mapId, float botX, float botY, float botZ, float minDistance, PositionInfo& outPos, uint32_t& outVendorEntry);
        static bool FindNearestLevelAppropriateCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId, float minDistance, PositionInfo& outPos, uint32_t& outCreatureEntry);
        static bool FindNearestGrindingCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
            float minDistance, int32_t minLevelOffset, int32_t maxLevelOffset,
            PositionInfo& outPos, uint32_t& outCreatureEntry);
    };
}
