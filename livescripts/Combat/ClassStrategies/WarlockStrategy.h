#pragma once
#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"
namespace Combat
{
    class WarlockStrategy : public IClassStrategy
    {
    public:
        const char* GetName() const override { return "WarlockStrategy"; }
        void UpdateCombat(Player*, Unit*, MovementManager*, const Blackboard::BotBlackboard&, uint32_t) override;
    private:
        Common::CooldownTimer _decisionTimer;
    };
}
