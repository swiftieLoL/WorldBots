#pragma once

#include <cstdint>

namespace Actions
{
    enum class ActionOutcome : uint8_t
    {
        Running,
        Succeeded,
        RetryableFailure,
        Blocked,
        Unsupported,
        Interrupted
    };

    enum class FailureCategory : uint8_t
    {
        None,
        Transient,
        Stalled,
        Navigation,
        Interaction,
        InventoryCapacity,
        ServiceCapability,
        ContentUnsupported,
        ProgressionDifficulty
    };

    enum class RecoveryDirective : uint8_t
    {
        None,
        RetryLater,
        Replan,
        GrindUntilLevel
    };
}
