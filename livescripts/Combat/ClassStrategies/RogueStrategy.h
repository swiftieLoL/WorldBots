#pragma once
#include "BaseCombatStrategy.h"

namespace Combat
{
    class RogueStrategy : public BaseCombatStrategy
    {
    public:
        const char* GetName() const override { return "RogueStrategy"; }

    protected:
        void ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard) override;

        bool ExecuteDisengageCC(Player* bot, Unit* threat,
            const Blackboard::BotBlackboard& blackboard) override;
    };
}
