#pragma once

#include "Blackboard/BotBlackboard.h"
#include "Helper/MovementManager.h"
#include "Player.h"
#include <functional>
#include <string>

namespace Actions
{
    struct ObjectiveContext
    {
        Player* bot;
        MovementManager* movement;
        const Blackboard::BotBlackboard& blackboard;
        const Blackboard::ActiveQuest& activeQuest;
        uint32_t deltaMs;
        std::function<bool(float, float, float, const char*)> moveToObjective;

        bool TryMoveTo(float x, float y, float z, const char* pathSource)
        {
            if (moveToObjective)
                return moveToObjective(x, y, z, pathSource);
            if (!movement)
                return false;
            movement->SetDiagnosticPathSource(pathSource ? pathSource : "quest_objective");
            return movement->MoveTo(x, y, z,
                BotMovementState::Moving, false);
        }
    };

    enum class ObjectiveResult : uint8_t
    {
        Handled,        // Handler consumed tick (caller returns)
        NotApplicable,  // Objective type not handled by this class
        Completed,      // Objective finished successfully
        Failed          // Objective failed
    };

    struct ObjectiveOutcome
    {
        ObjectiveResult result = ObjectiveResult::NotApplicable;
        std::string failureReason;
    };
}
