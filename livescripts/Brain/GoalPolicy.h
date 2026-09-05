#pragma once

#include "BotGoal.h"

namespace Brain
{
    struct ImmediateGoalInput
    {
        BotGoal currentGoal = BotGoal::Idle;
        bool inCombat = false;
        bool hasCombatTarget = false;
        bool hasCombatActionTarget = false;
        bool hasPartyTarget = false;
        bool preserveFlee = false;
        bool preserveCombat = false;
        bool preserveProgressQuest = false;
        bool preserveGrind = false;
        bool preserveNonInterruptible = false;
        bool shouldFlee = false;
        bool shouldRevivePartyMember = false;
        bool shouldRest = false;
    };

    struct ImmediateGoalDecision
    {
        bool handled = false;
        BotGoal goal = BotGoal::Idle;
    };

    ImmediateGoalDecision SelectImmediateGoal(const ImmediateGoalInput& input);
}
