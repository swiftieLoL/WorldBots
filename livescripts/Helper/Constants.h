#pragma once

#include <cstdint>

namespace Constants
{
    // Interaction Ranges (yards)
    constexpr float InteractionRange = 4.5f;
    constexpr float QuestInteractionRange = 4.0f;
    constexpr float VendorInteractionRange = InteractionRange;
    constexpr float LootInteractionRange = 3.0f;
    constexpr float DefaultNpcSearchRadius = 15.0f;
    constexpr float TacticalScanRadius = 30.0f;
    constexpr float QuestScanRadius = 100.0f;
    constexpr float MaxCombatChaseRange = 30.0f;
    constexpr float MinFleeDistance = 5.0f;
    constexpr float MaxFleeDistance = 40.0f;
    constexpr float DefaultFleeStepDistance = 15.0f;
    constexpr float StuckPositionThreshold = 0.5f;

    // Timeouts & Durations (ms)
    constexpr uint32_t FailsafeMovingTimeoutMs = 60000;
    constexpr uint32_t FailsafeIdleTimeoutMs = 15000;
    constexpr uint32_t LootFailsafeMovingTimeoutMs = 15000;
    constexpr uint32_t LootFailsafeIdleTimeoutMs = 5000;
    constexpr uint32_t WanderPauseIntervalMs = 3000;
    constexpr uint32_t StoppedMovementTimeoutMs = 1500;

    // Unit NPC Flags
    constexpr uint32_t UnitNpcFlagVendor = 128;
    constexpr uint32_t UnitNpcFlagRepair = 4096;
}
