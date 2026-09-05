#include "GoalPolicy.h"

namespace Brain
{
    ImmediateGoalDecision SelectImmediateGoal(const ImmediateGoalInput& input)
    {
        if (input.shouldFlee)
            return { true, BotGoal::Flee };

        // FleeAction owns the escape until combat and attacker observations
        // remain clear, or its timeout requests recovery. Do not cancel it on
        // a transient sense gap while the threat is still disengaging.
        if (input.preserveFlee)
            return { true, input.currentGoal };

        // The active action owns a live target even if a periodic sense slice
        // briefly omits it. Let CombatAction finish or invalidate that target
        // instead of bouncing through a fallback goal.
        if (input.preserveCombat)
            return { true, input.currentGoal };

        if (input.inCombat || input.hasCombatTarget || input.hasPartyTarget)
        {
            bool hasActiveEngagement = input.hasCombatTarget || input.hasPartyTarget;
            if (input.preserveProgressQuest && hasActiveEngagement)
                return { true, input.currentGoal };
            if (input.hasCombatActionTarget && !input.preserveProgressQuest &&
                (!input.preserveGrind || input.hasPartyTarget))
                return { true, BotGoal::Combat };
        }

        // Tactical combat and flee decisions above may interrupt an action.
        // Normal progression, loot, rest, and fallback decisions must wait for
        // an action that explicitly owns a non-interruptible transaction.
        if (input.preserveNonInterruptible)
            return { true, input.currentGoal };

        if (input.shouldRevivePartyMember)
            return { true, BotGoal::RevivePartyMember };
        if (input.shouldRest)
            return { true, BotGoal::Rest };
        return {};
    }
}
