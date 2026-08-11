#pragma once

#include "Globals/ObjectMgr.h"
#include "ObjectGuid.h"
#include "Helper/CommonTypes.h"
#include <vector>
#include <unordered_map>

namespace Blackboard
{
    using PositionInfo = Common::PositionInfo;

    enum class QuestTargetKind : uint8_t
    {
        None = 0,
        Creature,
        GameObject,
        Area
    };

    enum class QuestObjectiveType : uint8_t
    {
        KillCreature = 0,
        CollectItem,
        TalkToCreature,
        CastOnCreature,
        InteractGameObject,
        Explore,
        Unsupported
    };

    struct QuestObjectiveData
    {
        uint32_t targetEntry = 0;
        uint32_t itemId = 0;
        QuestObjectiveType type = QuestObjectiveType::Unsupported;
        QuestTargetKind targetKind = QuestTargetKind::None;
        uint32_t currentCount = 0;
        uint32_t requiredCount = 0;
        PositionInfo location;
        bool hasLocation = false;
    };

    struct KnownQuest
    {
        uint32_t questId = 0;
        ObjectGuid questGiverGuid;
        uint32_t questGiverEntry = 0;
        PositionInfo questGiverPosition;
        QuestTargetKind questGiverKind = QuestTargetKind::Creature;
        bool hasQuestGiverPosition = false;
    };

    struct ActiveQuest
    {
        uint32_t questId = 0;
        uint32_t targetNpcEntry = 0;
        QuestTargetKind targetKind = QuestTargetKind::None;
        PositionInfo targetPosition;
        bool hasTargetPosition = false;
        std::vector<QuestObjectiveData> objectives;
        bool isTalkOrTravelOnly = false;
        bool requiresTravel = false;
        bool requiresExploration = false;
        float explorationRadius = 0.0f;
        bool hasUnsupportedObjective = false;
    };

    struct ReadyToTurnInQuest
    {
        uint32_t questId = 0;
        ObjectGuid questGiverGuid;
        uint32_t questGiverEntry = 0;
        PositionInfo turnInPosition;
        QuestTargetKind questGiverKind = QuestTargetKind::Creature;
        bool hasTurnInPosition = false;
    };

    struct CachedItemSource
    {
        uint32_t resolvedEntry = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32_t mapId = 0;
        uint32_t lastKnownCount = 0;
        bool sourceResolutionAttempted = false;
        QuestTargetKind targetKind = QuestTargetKind::None;
        bool hasLocation = false;
    };

    struct QuestState
    {
        uint32_t refreshIntervalMs = 1000;
        uint32_t elapsedMs = 1000; // Trigger initial scan immediately on tick 1

        std::vector<KnownQuest> availableQuests;
        std::vector<ActiveQuest> activeQuests;
        std::vector<ReadyToTurnInQuest> completedQuests;

        // Cached resolved item loot sources to avoid expensive re-evaluation every tick
        // Quest-scoped because the same item can be a delivered source item
        // in one quest and a normal loot objective in another.
        std::unordered_map<uint64_t, CachedItemSource> itemSourceCache;
        uint32_t fullRescanTimerMs = 0;
        static constexpr uint32_t FullRescanIntervalMs = 30000;
    };

    struct SelfState
    {
        uint32_t refreshIntervalMs = 100;
        uint32_t elapsedMs = 100;

        uint32_t health = 0;
        uint32_t maxHealth = 0;
        uint32_t mana = 0;
        uint32_t maxMana = 0;
        uint8_t healthPct = 100;
        uint8_t manaPct = 100;
        bool isLowHealth = false;
        bool isLowMana = false;

        uint32_t level = 1;
        uint32_t money = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float orientation = 0.0f;
        uint32_t mapId = 0;
        uint32_t areaId = 0;
        uint32_t zoneId = 0;
        bool inCombat = false;
        bool isDead = false;
        bool isMounted = false;
        bool isSwimming = false;

        // Crowd-Control & Aura Perception
        bool isStunned = false;
        bool isFeared = false;
        bool isSilenced = false;
        bool isRooted = false;
        bool isCCed = false;
    };

    struct SpatialState
    {
        uint32_t refreshIntervalMs = 200;
        uint32_t elapsedMs = 200;

        ObjectGuid nearestEnemyGuid;
        ObjectGuid nearestEnemyInLoSGuid;
        ObjectGuid nearestFriendlyGuid;
        ObjectGuid nearestNpcGuid;
        ObjectGuid nearestCorpseGuid;
        ObjectGuid nearestGameObjectGuid;
        std::vector<ObjectGuid> hostileGuids;
        std::vector<ObjectGuid> nearbyPlayerGuids;
        std::vector<ObjectGuid> nearbyCorpseGuids;
        std::vector<ObjectGuid> nearbyGameObjectGuids;
    };

    struct PartyState
    {
        uint32_t refreshIntervalMs = 200;
        uint32_t elapsedMs = 200;

        bool isInGroup = false;
        bool isGroupLeader = false;
        ObjectGuid groupLeaderGuid;
        ObjectGuid lowestHealthGroupMemberGuid;
        ObjectGuid groupTargetGuid;
        std::vector<ObjectGuid> memberGuids;
    };

    struct CombatState
    {
        uint32_t refreshIntervalMs = 100;
        uint32_t elapsedMs = 100;

        ObjectGuid currentTargetGuid;
        ObjectGuid primaryAttackerGuid;
        std::vector<ObjectGuid> attackerGuids;
        uint32_t totalThreat = 0;
        bool spellReady = true;
    };

    struct NavigationState
    {
        uint32_t refreshIntervalMs = 100;
        uint32_t elapsedMs = 100;

        float destinationX = 0.0f;
        float destinationY = 0.0f;
        float destinationZ = 0.0f;
        uint8_t movementState = 0; // BotMovementState
        bool hasActivePath = false;
        bool isStuck = false;
    };

    struct InventoryState
    {
        uint32_t refreshIntervalMs = 1000;
        uint32_t elapsedMs = 1000;

        uint32_t freeBagSlots = 0;
        uint32_t totalBagSlots = 0;
        uint8_t durabilityPct = 100;
        bool needsRepair = false;
        bool hasItemsToSell = false;
        bool bagsFull = false;
        bool lowBagSpace = false;

        bool hasHealthPotion = false;
        bool hasManaPotion = false;
        bool hasFood = false;
        bool hasWater = false;

        uint32_t nearestVendorEntry = 0;
        PositionInfo vendorPosition;
    };

    struct BotBlackboard
    {
        SelfState self;
        SpatialState spatial;
        PartyState party;
        CombatState combat;
        NavigationState nav;
        InventoryState inv;
        QuestState quest;
    };
}
