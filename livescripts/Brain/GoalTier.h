#pragma once

#include <cstdint>

namespace Brain
{
    /// Priority tiers for goal evaluation, ordered highest to lowest.
    /// Goals in a higher tier are evaluated before (and preempt) lower tiers.
    enum class GoalTier : uint8_t
    {
        /// Tier 0: Bot is dead or dying. Non-negotiable.
        /// Goals: Resurrect, WaitForPartyResurrection, Unstuck (circuit breaker)
        Survival = 0,

        /// Tier 1: Immediate tactical response. Handled by SelectImmediateGoal().
        /// Goals: Flee, Combat, RevivePartyMember, Rest
        Tactical = 1,

        /// Tier 2: Capacity/health emergency blocking progress.
        /// Goals: TownRun (emergency inventory/durability), Loot (post-combat)
        Emergency = 2,

        /// Tier 3: Planned progression and servicing.
        /// Goals: TownRun (routine), QuestTurnIn, QuestProgress, QuestAccept
        Progression = 3,

        /// Tier 4: Party coordination when no individual work available.
        /// Goals: FollowTarget (party sync)
        Coordination = 4,

        /// Tier 5: Fallback behaviors.
        /// Goals: Grind, Wander, Idle
        Fallback = 5
    };

    inline const char* GoalTierToString(GoalTier tier)
    {
        switch (tier)
        {
            case GoalTier::Survival:    return "Survival";
            case GoalTier::Tactical:    return "Tactical";
            case GoalTier::Emergency:   return "Emergency";
            case GoalTier::Progression: return "Progression";
            case GoalTier::Coordination: return "Coordination";
            case GoalTier::Fallback:    return "Fallback";
            default:                    return "Unknown";
        }
    }
}
