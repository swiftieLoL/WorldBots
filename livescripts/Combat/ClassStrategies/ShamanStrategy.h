#pragma once
#include "IClassStrategy.h"
#include "Helper/CommonTypes.h"
namespace Combat
{
    class ShamanStrategy : public IClassStrategy
    {
    public:
        const char* GetName() const override { return "ShamanStrategy"; }
        void UpdateCombat(Player*, Unit*, MovementManager*, const Blackboard::BotBlackboard&, uint32_t) override;
    private:
        Common::CooldownTimer _decisionTimer;
    };
}
