#pragma once

#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Unit.h"
#include "Helper/MovementManager.h"
#include "Blackboard/BotBlackboard.h"
#include <cstdint>

namespace Combat
{
    class IClassStrategy
    {
    public:
        virtual ~IClassStrategy() = default;

        virtual const char* GetName() const = 0;

        virtual void UpdateCombat(
            Player* bot,
            Unit* target,
            MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard,
            uint32_t deltaMs) = 0;

        virtual bool TryDisengageCC(
            Player* /*bot*/,
            Unit* /*threat*/,
            const Blackboard::BotBlackboard& /*blackboard*/)
        {
            return false;
        }
    };
}
