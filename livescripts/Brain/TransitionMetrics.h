#pragma once

#include "BotGoal.h"

#include <cstdint>

namespace Brain
{
    struct TransitionMetrics
    {
        uint32_t totalTransitions = 0;
        uint32_t recentTransitions = 0;
        uint32_t recentInterruptedTransitions = 0;
        uint32_t windowStartMs = 0;
        uint32_t interruptedActions = 0;
        uint32_t contextRefreshes = 0;
        BotGoal recentGoals[6] = {};
        uint32_t goalIdx = 0;
        bool churnReportedInWindow = false;

        static constexpr uint32_t CHURN_WINDOW_MS = 15000;
        static constexpr uint32_t CHURN_THRESHOLD = 6;

        bool IsChurning() const
        {
            return recentInterruptedTransitions >= CHURN_THRESHOLD;
        }

        // Returns true only when this transition first crosses the churn
        // threshold for the current window. Completed-action transitions are
        // useful telemetry, but only interruptions of running actions are
        // evidence that the planner is thrashing.
        bool RecordTransition(BotGoal goal, uint32_t nowMs, bool interruptedRunningAction)
        {
            totalTransitions++;
            if (recentTransitions == 0 || nowMs - windowStartMs >= CHURN_WINDOW_MS)
            {
                recentTransitions = 0;
                recentInterruptedTransitions = 0;
                windowStartMs = nowMs;
                churnReportedInWindow = false;
            }

            recentTransitions++;
            if (interruptedRunningAction)
                recentInterruptedTransitions++;
            recentGoals[goalIdx % 6] = goal;
            goalIdx++;

            if (!churnReportedInWindow && IsChurning())
            {
                churnReportedInWindow = true;
                return true;
            }
            return false;
        }
    };
}
