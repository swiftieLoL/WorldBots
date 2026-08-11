#include "CombatAction.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellHistory.h"
#include "Log.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace Actions
{
    CombatAction::CombatAction(ObjectGuid targetGuid)
        : _targetGuid(targetGuid), _started(false), _completed(false)
    {
    }

    void CombatAction::Start(Player* bot, MovementManager* movement)
    {
        if (!bot || !bot->IsInWorld() || !_targetGuid)
        {
            _completed = true;
            return;
        }

        Unit* target = ObjectAccessor::GetUnit(*bot, _targetGuid);
        if (!target || !target->IsAlive())
        {
            _completed = true;
            return;
        }

        _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
        _started = true;
    }

    void CombatAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld() || _completed)
            return;

        Unit* target = ObjectAccessor::GetUnit(*bot, _targetGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive() || target->GetMap() != bot->GetMap())
        {
            if (bot->GetVictim())
            {
                bot->AttackStop();
            }
            if (movement)
            {
                movement->Stop();
            }
            _completed = true;
            return;
        }

        if (!_classStrategy)
        {
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
        }

        if (_classStrategy)
        {
            _classStrategy->UpdateCombat(bot, target, movement, blackboard, deltaMs);
        }
    }

    void CombatAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot)
        {
            bot->AttackStop();
        }
        if (movement)
        {
            movement->Stop();
        }
    }

    bool CombatAction::IsComplete() const
    {
        return _completed;
    }
}
