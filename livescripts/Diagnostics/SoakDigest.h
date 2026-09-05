#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Diagnostics
{
    enum class SoakEvent : uint8_t
    {
        Deaths,               // Total deaths this window
        CircuitBreakerFired,  // Death circuit breaker activations
        QuestsSuppressed,     // Quests suppressed due to failure
        NpcsSuppressed,       // NPCs blacklisted due to failure
        GoalChurns,           // Churn episodes (6+ running-action interruptions in 15s)
        InventoryDeadlocks,   // Inventory cleanup deferrals (all items protected)
        StuckEscalations,     // Stuck timer hit 8s+ (wide evasion or teleport)
        ActionBugs,           // Action completed while still reporting Running
        ActionsInterrupted,   // Actions interrupted by goal changes
        _Count
    };

    class SoakDigest
    {
    public:
        /// Increment a counter. Called inline at the event site — no logging, no allocation.
        static void Record(uint32_t guidLow, SoakEvent event, uint32_t amount = 1);

        /// Update the cached bot name/level for digest output.
        static void Touch(uint32_t guidLow, const char* name, uint8_t level);

        /// Lifetime total since runtime initialization. Used by the progress
        /// journal even when the 30-minute console digest resets its window.
        static uint32_t GetTotal(uint32_t guidLow, SoakEvent event);

        /// Emit one summary line per bot, then reset all counters.
        static void EmitDigest();

        /// Remove a single bot's counters when destroyed/stopped.
        static void Remove(uint32_t guidLow);

        /// Clear all state (on shutdown/reinit).
        static void Clear();

    private:
        static constexpr size_t EVENT_COUNT = static_cast<size_t>(SoakEvent::_Count);

        struct BotDigest
        {
            std::array<uint32_t, EVENT_COUNT> counts{};
            std::array<uint32_t, EVENT_COUNT> totals{};
            std::string name;
            uint8_t level = 0;
        };

        static std::unordered_map<uint32_t, BotDigest> s_digests;
        static std::mutex s_mutex;
    };
}
