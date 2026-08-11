#pragma once

#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"

namespace Combat
{
    class HunterStrategy : public IClassStrategy
    {
    public:
        const char* GetName() const override { return "HunterStrategy"; }

        void UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;

    private:
        Common::CooldownTimer _decisionTimer;
    };
}
