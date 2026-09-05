#pragma once

#include "Diagnostics/SoakDigest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diagnostics
{
    struct ProgressSample
    {
        uint32_t guidLow = 0;
        std::string name;
        bool runtimeActive = true;
        uint8_t level = 0;
        uint32_t xp = 0;
        uint32_t nextLevelXp = 0;
        std::string profile;
        std::string goal;
        std::string tier;
        std::string action;
        std::string outcome;
        std::string actionDetail;
        uint32_t actionElapsedMs = 0;
        uint32_t actionIdleMs = 0;
        std::string travelMode;
        std::string travelWaitReason;
        bool casting = false;
        uint32_t travelElapsedMs = 0;
        uint32_t travelStepElapsedMs = 0;
        uint32_t travelReplans = 0;
        uint32_t travelStepIndex = 0;
        uint32_t travelStepCount = 0;
        uint32_t activeQuestId = 0;
        uint32_t watchdogQuestId = 0;
        uint32_t watchdogMs = 0;
        uint32_t deathRecoverySeconds = 0;
        uint32_t mapId = 0;
        uint32_t zoneId = 0;
        uint32_t areaId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint8_t healthPct = 0;
        uint8_t manaPct = 0;
        bool dead = false;
        bool inCombat = false;
        bool snapshotReady = false;
        std::string movement;
        bool hasPath = false;
        std::string pathFailure;
        uint32_t pathFlags = 0;
        uint64_t pathAttemptGeneration = 0;
        bool pathEvidenceFresh = false;
        uint32_t originPathFailures = 0;
        uint32_t originPathDestinations = 0;
        bool originRecoveryRequired = false;
        float pathRequestX = 0.0f;
        float pathRequestY = 0.0f;
        float pathRequestZ = 0.0f;
        bool pathEndpointAvailable = false;
        float pathEndpointX = 0.0f;
        float pathEndpointY = 0.0f;
        float pathEndpointZ = 0.0f;
        std::string externalControl;
        bool navStuck = false;
        uint32_t freeBagSlots = 0;
        uint32_t totalBagSlots = 0;
        bool bagsFull = false;
        bool needsRepair = false;
        bool needsRestock = false;
        uint32_t availableQuests = 0;
        uint32_t activeQuests = 0;
        uint32_t completedQuests = 0;
        uint32_t totalTransitions = 0;
        uint32_t recentInterruptedTransitions = 0;
        bool churning = false;
        std::array<uint32_t, static_cast<size_t>(SoakEvent::_Count)> soakTotals{};
    };

    class ProgressMonitor
    {
    public:
        static void Configure(bool enabled, std::string directory, uint32_t stallSeconds);
        static void WriteSnapshot(std::vector<ProgressSample> samples);
        static void Remove(uint32_t guidLow);
        static void Reset();
        static bool IsEnabled();
        static std::string GetLatestPath();
        static std::string GetHistoryPath();

    private:
        struct ProgressState
        {
            uint8_t level = 0;
            uint32_t xp = 0;
            uint64_t lastProgressMs = 0;
        };

        static std::string Classify(const ProgressSample& sample, uint64_t progressAgeSeconds);
        static std::unordered_map<uint64_t, ProgressState> s_progress;
        static std::mutex s_mutex;
        static bool s_enabled;
        static uint32_t s_stallSeconds;
        static std::string s_latestPath;
        static std::string s_historyPath;
    };
}
