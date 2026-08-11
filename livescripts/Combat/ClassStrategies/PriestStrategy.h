#pragma once

#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"

namespace Combat
{
    class PriestStrategy : public IClassStrategy
    {
    public:
        PriestStrategy() = default;
        ~PriestStrategy() override = default;

        const char* GetName() const override { return "PriestStrategy"; }

        void UpdateCombat(
            Player* bot,
            Unit* target,
            MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard,
            uint32_t deltaMs) override;

    private:
        Common::CooldownTimer _cooldownTimer;
    };
}
