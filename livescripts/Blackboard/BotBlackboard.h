#pragma once

#include "ObjectGuid.h"
#include "Helper/CommonTypes.h"
#include "Party/PartyCoordination.h"
#include "Brain/SuppressionRegistry.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <limits>

namespace Blackboard
{
    using PositionInfo = Common::PositionInfo;

    struct RefreshState
    {
        bool initialized = false;
        uint32_t ageMs = std::numeric_limits<uint32_t>::max();
    };

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
        // targetEntry is the entry credited by the quest log. Some item-use
        // quests credit a synthetic entry while their spell conditions name a
        // different live creature or GameObject. Keep both identities so
        // sensing can route to the real entity without fabricating credit.
        uint32_t targetEntry = 0;
        uint32_t interactionEntry = 0;
        uint32_t itemId = 0;
        uint32_t sourceItemId = 0;
        uint32_t sourceSpellId = 0;
        QuestObjectiveType type = QuestObjectiveType::Unsupported;
        QuestTargetKind targetKind = QuestTargetKind::None;
        QuestTargetKind interactionKind = QuestTargetKind::None;
        bool sourceSpellTargetsEntity = false;
        uint32_t currentCount = 0;
        uint32_t requiredCount = 0;
        PositionInfo location;
        bool hasLocation = false;
        bool combatLevelSuitable = true;
        bool vendorPurchase = false;
    };

    struct KnownQuest
    {
        uint32_t questId = 0;
        ObjectGuid questGiverGuid;
        uint32_t questGiverEntry = 0;
        PositionInfo questGiverPosition;
        QuestTargetKind questGiverKind = QuestTargetKind::Creature;
        bool hasQuestGiverPosition = false;
        bool isGlobalDiscovery = false;
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
        bool vendorPurchase = false;
    };

    struct QuestState : RefreshState
    {
        uint32_t refreshIntervalMs = 1000;
        uint32_t elapsedMs = 1000; // Trigger initial scan immediately on tick 1

        std::vector<KnownQuest> availableQuests;
        std::vector<ActiveQuest> activeQuests;
        std::vector<ReadyToTurnInQuest> completedQuests;

        // Supplied by BotBrain before the quest sense pass. Discovery uses
        // this set to move past timed, level-deferred, and unsupported quests
        // instead of continually republishing the same cached world starter.
        std::unordered_set<uint32_t> excludedQuestIds;
        // Supplied by BotBrain alongside excludedQuestIds. This lets both the
        // nearby scan and cached world-starter discovery move past a quest hub
        // whose first executable travel leg has repeatedly failed for this bot.
        std::vector<Brain::DestinationSuppression> excludedQuestDestinations;

        // Cached resolved item loot sources to avoid expensive re-evaluation every tick
        // Quest-scoped because the same item can be a delivered source item
        // in one quest and a normal loot objective in another.
        std::unordered_map<uint64_t, CachedItemSource> itemSourceCache;
        uint32_t fullRescanTimerMs = 0;
        static constexpr uint32_t FullRescanIntervalMs = 30000;

        // Global starter discovery is substantially more expensive than the
        // nearby live scan. Cache both successful and unsuccessful searches so
        // a bot without local quest work does not rescan the world every second.
        KnownQuest cachedWorldStarter;
        bool hasCachedWorldStarter = false;
        bool worldStarterScanAttempted = false;
        uint32_t worldStarterScanElapsedMs = 30000;
        static constexpr uint32_t WorldStarterRescanIntervalMs = 30000;
    };

    struct SelfState : RefreshState
    {
        uint32_t refreshIntervalMs = 100;
        uint32_t elapsedMs = 100;

        uint32_t health = 0;
        uint32_t maxHealth = 0;
        uint32_t mana = 0;
        uint32_t maxMana = 0;
        uint8_t healthPct = 100;
        uint8_t manaPct = 100;

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

    struct SpatialState : RefreshState
    {
        uint32_t refreshIntervalMs = 200;
        uint32_t elapsedMs = 200;

        ObjectGuid nearestEnemyGuid;
        ObjectGuid nearestFriendlyGuid;
        std::vector<ObjectGuid> hostileGuids;
    };

    struct PartyState : RefreshState
    {
        uint32_t refreshIntervalMs = 200;
        uint32_t elapsedMs = 200;

        bool isInGroup = false;
        bool isGroupLeader = false;
        bool leaderOnSameMap = false;
        ObjectGuid groupLeaderGuid;
        ObjectGuid tankGuid;
        ObjectGuid healerGuid;
        ObjectGuid designatedResurrectorGuid;
        ObjectGuid deadGroupMemberGuid;
        ObjectGuid laggingQuestMemberGuid;
        ObjectGuid lowestHealthGroupMemberGuid;
        ObjectGuid groupTargetGuid;
        Party::Role role = Party::Role::None;
        uint8_t lowestHealthGroupMemberPct = 100;
        float leaderDistance = 0.0f;
        float formationDistance = 2.0f;
        float formationAngle = 0.0f;
        std::vector<uint32_t> leaderQuestIds;
        uint32_t laggingQuestId = 0;
        std::vector<ObjectGuid> memberGuids;
    };

    struct CombatState : RefreshState
    {
        uint32_t refreshIntervalMs = 100;
        uint32_t elapsedMs = 100;

        ObjectGuid currentTargetGuid;
        ObjectGuid primaryAttackerGuid;
        std::vector<ObjectGuid> attackerGuids;
    };

    struct NavigationState : RefreshState
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

    struct InventoryState : RefreshState
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
        bool needsRestock = false;

        uint32_t nearestVendorEntry = 0;
        PositionInfo vendorPosition;
    };

    struct BotBlackboard
    {
        uint64_t generation = 0;
        bool initialSnapshotReady = false;
        SelfState self;
        SpatialState spatial;
        PartyState party;
        CombatState combat;
        NavigationState nav;
        InventoryState inv;
        QuestState quest;
    };
}
