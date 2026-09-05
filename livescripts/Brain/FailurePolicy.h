#pragma once

#include "Actions/ActionTypes.h"
#include "BotGoal.h"

#include <cstdint>

namespace Brain
{
    bool IsFailureOutcome(Actions::ActionOutcome outcome);
    uint32_t GetSuppressionSeconds(Actions::ActionOutcome outcome);
    Actions::ActionOutcome NormalizeTerminalOutcome(bool complete,
        Actions::ActionOutcome outcome);
    bool ShouldApplyRestockBackoff(BotGoal goal, Actions::ActionOutcome outcome,
        bool stillNeedsRestock);
    bool ShouldApplyInventoryCleanupBackoff(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category);
    bool ShouldApplyTownServiceBackoff(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category);
    uint32_t GetTownServiceBackoffSeconds(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category);
    bool ShouldSuppressQuest(Actions::ActionOutcome outcome,
        Actions::FailureCategory category, Actions::RecoveryDirective directive);
    bool ShouldGrindUntilLevel(Actions::FailureCategory category,
        Actions::RecoveryDirective directive);
    bool ShouldRecordNavigationFailure(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category);
    bool ShouldRelocateForNavigationFailure(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category,
        uint32_t pathFlags, bool navigationStuck, bool hasFreshPathEvidence);
}
