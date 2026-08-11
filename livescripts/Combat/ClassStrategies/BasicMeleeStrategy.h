#pragma once

#include "IClassStrategy.h"

namespace Combat
{
    class BasicMeleeStrategy : public IClassStrategy
    {
    public:
        const char* GetName() const override { return "BasicMeleeStrategy"; }

        void UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
    };
}
