#pragma once

#include "BaseCombatStrategy.h"

namespace Combat
{
    class WarriorStrategy : public BaseCombatStrategy
    {
    public:
        WarriorStrategy() = default;
        ~WarriorStrategy() override = default;

        const char* GetName() const override { return "WarriorStrategy"; }

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
