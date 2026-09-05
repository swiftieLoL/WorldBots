#pragma once

#include "IClassStrategy.h"
#include "ClassStrategyUtils.h"
#include "Helper/CommonTypes.h"

namespace Combat
{
    class BaseCombatStrategy : public IClassStrategy
    {
    public:
        ~BaseCombatStrategy() override = default;

        void UpdateCombat(
            Player* bot,
            Unit* target,
            MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard,
            uint32_t deltaMs) override
        {
            if (!ClassStrategyUtils::IsValid(bot, target))
                return;

            _decisionTimer.Tick(deltaMs);
            ExecuteCombat(bot, target, movement, blackboard);
        }

        bool TryDisengageCC(
            Player* bot,
            Unit* threat,
            const Blackboard::BotBlackboard& blackboard) override
        {
            if (!bot || !bot->IsInWorld() || !threat || !threat->IsInWorld() ||
                !threat->IsAlive() || threat->GetMap() != bot->GetMap())
                return false;

            return ExecuteDisengageCC(bot, threat, blackboard);
        }

    protected:
        virtual void ExecuteCombat(
            Player* bot,
            Unit* target,
            MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard) = 0;

        virtual bool ExecuteDisengageCC(
            Player* /*bot*/,
            Unit* /*threat*/,
            const Blackboard::BotBlackboard& /*blackboard*/)
        {
            return false;
        }

        Common::CooldownTimer _decisionTimer;
    };
}
