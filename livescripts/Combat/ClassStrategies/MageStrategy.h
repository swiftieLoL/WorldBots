#pragma once

#include "BaseCombatStrategy.h"

namespace Combat
{
    class MageStrategy : public BaseCombatStrategy
    {
    public:
        MageStrategy() = default;
        ~MageStrategy() override = default;

        const char* GetName() const override { return "MageStrategy"; }

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
