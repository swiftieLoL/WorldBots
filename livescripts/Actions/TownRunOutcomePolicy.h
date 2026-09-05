#pragma once

#include "Actions/ActionTypes.h"

namespace Actions
{
    struct TownRunTerminalOutcome
    {
        ActionOutcome outcome = ActionOutcome::Succeeded;
        FailureCategory failureCategory = FailureCategory::None;
        RecoveryDirective recoveryDirective = RecoveryDirective::None;
        bool inventoryCapacityFailure = false;
    };

    constexpr TownRunTerminalOutcome ResolveTownRunTerminalOutcome(
        bool blockedByMissingVendor, bool blockedByProtectedInventory)
    {
        if (blockedByMissingVendor)
        {
            return {
                ActionOutcome::Blocked,
                FailureCategory::ServiceCapability,
                RecoveryDirective::RetryLater,
                false
            };
        }

        if (blockedByProtectedInventory)
        {
            return {
                ActionOutcome::Blocked,
                FailureCategory::InventoryCapacity,
                RecoveryDirective::Replan,
                true
            };
        }

        // An empty plan that accomplished nothing is a failure, not a success.
        return {
            ActionOutcome::RetryableFailure,
            FailureCategory::ServiceCapability,
            RecoveryDirective::RetryLater,
            false
        };
    }
}
