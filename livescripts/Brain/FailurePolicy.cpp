#include "FailurePolicy.h"

#include "Helper/MovementPathPolicy.h"

namespace Brain
{
    bool IsFailureOutcome(Actions::ActionOutcome outcome)
    {
        return outcome == Actions::ActionOutcome::Blocked ||
            outcome == Actions::ActionOutcome::Unsupported ||
            outcome == Actions::ActionOutcome::RetryableFailure;
    }

    uint32_t GetSuppressionSeconds(Actions::ActionOutcome outcome)
    {
        if (outcome == Actions::ActionOutcome::Unsupported)
            return 3600;
        if (outcome == Actions::ActionOutcome::Blocked)
            return 900;
        return outcome == Actions::ActionOutcome::RetryableFailure ? 60 : 0;
    }

    Actions::ActionOutcome NormalizeTerminalOutcome(bool complete,
        Actions::ActionOutcome outcome)
    {
        return complete && outcome == Actions::ActionOutcome::Running
            ? Actions::ActionOutcome::RetryableFailure
            : outcome;
    }

    bool ShouldApplyRestockBackoff(BotGoal goal, Actions::ActionOutcome outcome,
        bool stillNeedsRestock)
    {
        return goal == BotGoal::TownRun && stillNeedsRestock &&
            outcome != Actions::ActionOutcome::Running &&
            outcome != Actions::ActionOutcome::Interrupted;
    }

    bool ShouldApplyInventoryCleanupBackoff(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category)
    {
        return goal == BotGoal::TownRun &&
            category == Actions::FailureCategory::InventoryCapacity &&
            IsFailureOutcome(outcome);
    }

    uint32_t GetTownServiceBackoffSeconds(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category)
    {
        if (goal != BotGoal::TownRun || !IsFailureOutcome(outcome))
            return 0;

        if (category == Actions::FailureCategory::ServiceCapability)
            return 300;

        // A repair or selling destination can be known yet temporarily
        // unreachable. Without a navigation backoff, the unchanged inventory
        // state immediately reselects TownRun and creates a sub-second retry
        // storm. Keep this delay short so a newly viable route is retried
        // promptly while other progression gets a chance to run.
        if (category == Actions::FailureCategory::Navigation ||
            category == Actions::FailureCategory::Interaction)
            return 60;

        return 0;
    }

    bool ShouldApplyTownServiceBackoff(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category)
    {
        return GetTownServiceBackoffSeconds(goal, outcome, category) != 0;
    }

    bool ShouldSuppressQuest(Actions::ActionOutcome outcome,
        Actions::FailureCategory /*category*/, Actions::RecoveryDirective directive)
    {
        if (!IsFailureOutcome(outcome))
            return false;

        // Quests requesting Replan return to the planner immediately (e.g. to route to TownRun).
        // Other failures (such as RetryLater) suppress the quest so the bot does not enter a tight retry storm.
        return directive != Actions::RecoveryDirective::Replan;
    }

    bool ShouldGrindUntilLevel(Actions::FailureCategory category,
        Actions::RecoveryDirective directive)
    {
        return category == Actions::FailureCategory::ProgressionDifficulty &&
            directive == Actions::RecoveryDirective::GrindUntilLevel;
    }

    bool ShouldRecordNavigationFailure(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category)
    {
        // An escape can time out because combat remains active through a chain
        // pull even when every movement leg was valid. It is tactical state,
        // not evidence that the bot is stranded on a disconnected navmesh.
        return goal != BotGoal::Flee &&
            category == Actions::FailureCategory::Navigation &&
            IsFailureOutcome(outcome);
    }

    bool ShouldRelocateForNavigationFailure(BotGoal goal,
        Actions::ActionOutcome outcome, Actions::FailureCategory category,
        uint32_t pathFlags, bool navigationStuck, bool hasFreshPathEvidence)
    {
        if (!ShouldRecordNavigationFailure(goal, outcome, category))
            return false;

        // A rejected destination is evidence against that target, not against
        // the bot's current position. Relocation is reserved for an origin
        // that PathGenerator could not attach to the navmesh, or for the
        // independent stuck detector reporting a physical deadlock.
        return navigationStuck || (hasFreshPathEvidence &&
            (pathFlags & Helper::MovementPathPolicy::FarFromPolyStart) != 0);
    }
}
