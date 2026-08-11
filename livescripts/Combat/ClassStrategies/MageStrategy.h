#pragma once

#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"

namespace Combat
{
    class MageStrategy : public IClassStrategy
    {
    public:
        MageStrategy() = default;
        ~MageStrategy() override = default;

        const char* GetName() const override { return "MageStrategy"; }

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
