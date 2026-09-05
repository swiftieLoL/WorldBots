#pragma once

#include "Helper/CommonTypes.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <limits>
#include <functional>

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
        // Authored percentage from the direct or reference loot row. Zero
        // means the database did not expose a useful standalone chance (for
        // example, an equal-chance loot group).
        float dropChance = 0.0f;

        bool operator==(const LootSource& other) const
        {
            return type == other.type && entry == other.entry;
        }
    };

    struct SpellInteractionTarget
    {
        uint32_t entry = 0;
        bool isGameObject = false;
        bool targetsEntity = false;
    };

    struct GrindingSearchDiagnostics
    {
        uint32_t indexedCandidates = 0;
        uint32_t suppressed = 0;
        uint32_t inactiveEvent = 0;
        uint32_t nonHostile = 0;
        uint32_t unsafePosition = 0;
        uint32_t weakLevelFit = 0;
        uint32_t tooNear = 0;
        uint32_t tooFar = 0;
        uint32_t eligibleInRange = 0;
        uint32_t viableCells = 0;
    };

    class BotCache
    {
    public:
        static void Initialize();
        static bool IsInitialized();

        static bool FindNpcLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
        static std::vector<PositionInfo> GetNpcLocations(uint32_t entry);
        static bool FindGameObjectLocation(uint32_t entry, float& outX, float& outY, float& outZ, uint32_t& outMapId, float nearX = 0.0f, float nearY = 0.0f, float nearZ = 0.0f, uint32_t nearMapId = std::numeric_limits<uint32_t>::max());
        static std::vector<PositionInfo> GetGameObjectLocations(uint32_t entry);
        static bool FindNearestVendor(uint32_t mapId, float botX, float botY, float botZ,
            bool requireVendor, bool requireRepair, uint32_t& outVendorEntry, PositionInfo& outPos,
            std::function<bool(uint32_t)> entryFilter = {});
        static bool SuppressVendorLocation(uint32_t entry, uint32_t mapId, float x, float y, float z);
        static bool ConfirmVendorLocation(uint32_t entry, uint32_t mapId, float x, float y, float z);

        static const std::vector<uint32_t>& GetQuestEnders(uint32_t questId);
        static const std::vector<uint32_t>& GetQuestStarters(uint32_t creatureEntry);
        static const std::vector<uint32_t>& GetGameObjectQuestEnders(uint32_t questId);
        static const std::vector<uint32_t>& GetGameObjectQuestStarters(uint32_t gameObjectEntry);
        static std::vector<LootSource> GetItemLootSources(uint32_t itemId);
        static std::vector<VendorInfo> GetItemVendorSources(uint32_t itemId);
        static bool VendorSellsItem(uint32_t vendorEntry, uint32_t itemId);
        static bool IsCastCreditQuest(uint32_t questId);
        static std::vector<SpellInteractionTarget> GetSpellInteractionTargets(uint32_t spellId);

        static bool FindNearestAvailableQuestStarter(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
            PositionInfo& outPos, uint32_t& outQuestId, uint32_t& outEntry, bool& outIsGameObject,
            const std::unordered_set<uint32_t>& excludedQuestIds = {},
            std::function<bool(const PositionInfo&)> positionFilter = {});
        static bool FindNearestSettlementOrVendor(uint32_t mapId, float botX, float botY, float botZ, float minDistance, PositionInfo& outPos, uint32_t& outVendorEntry);
        static bool FindNearestSafeRecoveryHub(Player* bot, uint32_t mapId,
            float botX, float botY, PositionInfo& outPos, uint32_t& outCreatureEntry,
            const std::vector<PositionInfo>& excludedPositions = {},
            bool requireSuitableEcology = false);
        static bool FindNearestLevelAppropriateCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId, float minDistance, PositionInfo& outPos, uint32_t& outCreatureEntry);
        static bool FindNearestGrindingCreature(Player* bot, float botX, float botY, float botZ, uint32_t mapId,
            float minDistance, int32_t minLevelOffset, int32_t maxLevelOffset,
            PositionInfo& outPos, uint32_t& outCreatureEntry,
            const std::unordered_set<uint64_t>& excludedSpawnIds = {},
            const std::unordered_set<uint32_t>& excludedCreatureEntries = {},
            std::function<bool(const PositionInfo&)> positionFilter = {},
            float maxDistance = std::numeric_limits<float>::max(),
            uint64_t* outSpawnId = nullptr,
            GrindingSearchDiagnostics* diagnostics = nullptr);
        // Static spawn data is only a discovery index. Load the nominated
        // destination grid and replace that lead with a creature that exists
        // now, is visible in this bot's phase, and can actually be ground.
        static bool ResolveLiveGrindingAnchor(Player* bot,
            const PositionInfo& staticAnchor, float searchRadius,
            int32_t minLevelOffset, int32_t maxLevelOffset,
            PositionInfo& outPos, uint32_t& outCreatureEntry,
            uint64_t& outSpawnId,
            const std::unordered_set<uint64_t>& excludedSpawnIds = {},
            const std::unordered_set<uint32_t>& excludedCreatureEntries = {},
            std::function<bool(const PositionInfo&)> positionFilter = {});
        static bool FindViableGrindingArea(Player* bot, float botX, float botY, float botZ,
            uint32_t mapId, uint32_t currentZoneId, float minDistance,
            int32_t minLevelOffset, int32_t maxLevelOffset,
            PositionInfo& outPos, uint32_t& outCreatureEntry, uint64_t& outSpawnId,
            const std::unordered_set<uint64_t>& excludedSpawnIds = {},
            const std::unordered_set<uint32_t>& excludedCreatureEntries = {},
            std::function<bool(const PositionInfo&)> positionFilter = {},
            GrindingSearchDiagnostics* diagnostics = nullptr);
    };
}
