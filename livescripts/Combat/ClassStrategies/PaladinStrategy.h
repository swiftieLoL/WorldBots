#pragma once
#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"
namespace Combat
{
    class PaladinStrategy : public IClassStrategy
    {
    public:
        const char* GetName() const override { return "PaladinStrategy"; }
        void UpdateCombat(Player*, Unit*, MovementManager*, const Blackboard::BotBlackboard&, uint32_t) override;
    private:
        Common::CooldownTimer _decisionTimer;
    };
}
