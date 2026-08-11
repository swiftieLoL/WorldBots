#pragma once

#include <cstdint>

namespace Constants
{
    // Task Intervals (ms)
    constexpr uint32_t SenseTaskIntervalMs = 50;
    constexpr uint32_t CombatTaskIntervalMs = 50;
    constexpr uint32_t ActionTaskIntervalMs = 50;
    constexpr uint32_t MovementTaskIntervalMs = 100;
    constexpr uint32_t SpawnTaskIntervalMs = 100;
    constexpr uint32_t ThinkTaskIntervalMs = 500;
    constexpr uint32_t InventoryTaskIntervalMs = 1000;
    constexpr uint32_t QuestTaskIntervalMs = 1000;
    constexpr uint32_t DebugPositionTaskIntervalMs = 2000;
    constexpr uint32_t SaveTaskIntervalMs = 30000;

    // Interaction Ranges (yards)
    constexpr float QuestInteractionRange = 4.0f;
    constexpr float VendorInteractionRange = 4.5f;
    constexpr float LootInteractionRange = 3.0f;
    constexpr float DefaultNpcSearchRadius = 15.0f;
    constexpr float TacticalScanRadius = 30.0f;
    constexpr float QuestScanRadius = 100.0f;

    // Timeouts & Durations (ms or seconds)
    constexpr uint32_t FailsafeMovingTimeoutMs = 60000;
    constexpr uint32_t FailsafeIdleTimeoutMs = 15000;
    constexpr uint32_t LootFailsafeMovingTimeoutMs = 15000;
    constexpr uint32_t LootFailsafeIdleTimeoutMs = 5000;
    constexpr uint32_t WanderPauseIntervalMs = 3000;
    constexpr uint32_t StoppedMovementTimeoutMs = 1500;
    constexpr uint32_t IgnoredLootExpirySec = 180;

    // Emote State Codes
    constexpr uint32_t EmoteStateLoot = 228;
    constexpr uint32_t EmoteStateNone = 0;

    // Unit NPC Flags
    constexpr uint32_t UnitNpcFlagVendor = 128;
    constexpr uint32_t UnitNpcFlagRepair = 4096;
}
