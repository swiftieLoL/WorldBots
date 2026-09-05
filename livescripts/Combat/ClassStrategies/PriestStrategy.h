#pragma once

#include "BaseCombatStrategy.h"

namespace Combat
{
    class PriestStrategy : public BaseCombatStrategy
    {
    public:
        PriestStrategy() = default;
        ~PriestStrategy() override = default;

        const char* GetName() const override { return "PriestStrategy"; }

    protected:
        void ExecuteCombat(
            Player* bot,
            Unit* target,
            MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard) override;

        bool ExecuteDisengageCC(
            Player* bot,
            Unit* threat,
            const Blackboard::BotBlackboard& blackboard) override;
    };
}
