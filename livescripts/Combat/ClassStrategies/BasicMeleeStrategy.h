#pragma once

#include "BaseCombatStrategy.h"

namespace Combat
{
    class BasicMeleeStrategy : public BaseCombatStrategy
    {
    public:
        const char* GetName() const override { return "BasicMeleeStrategy"; }

    protected:
        void ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard) override;
    };
}
