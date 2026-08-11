#pragma once
#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"
namespace Combat
{
    class DruidStrategy : public IClassStrategy
    {
    public:
        const char* GetName() const override { return "DruidStrategy"; }
        void UpdateCombat(Player*, Unit*, MovementManager*, const Blackboard::BotBlackboard&, uint32_t) override;
    private:
        Common::CooldownTimer _decisionTimer;
    };
}
