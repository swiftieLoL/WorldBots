#include "GrindAction.h"

#include "Blackboard/BotBlackboard.h"
#include "Cache/BotCache.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "Creature.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/MathUtils.h"
#include "Helper/ProgressionPolicy.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Actions
{
    GrindAction::GrindAction(int32_t minLevelOffset, int32_t maxLevelOffset)
        : _minLevelOffset(minLevelOffset), _maxLevelOffset(maxLevelOffset)
    {
    }

    void GrindAction::Start(Player* bot, MovementManager* /*movement*/)
    {
        _targetGuid.Clear();
        _targetSearchCooldownMs = 0;
        _destinationRefreshMs = 0;
        if (bot)
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
    }

    bool GrindAction::IsSafeTarget(Player* bot, Creature* creature) const
    {
        if (!bot || !creature || !creature->IsAlive() || creature->IsCritter() || creature->IsCivilian() ||
            !creature->isTargetableForAttack() || !bot->IsValidAttackTarget(creature))
            return false;

        CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate();
        if (!creatureTemplate)
            return false;

        bool isAttackingBot = creature->GetVictim() == bot || creature->IsInCombatWith(bot);
        if (!isAttackingBot)
        {
            if (creatureTemplate->rank != CREATURE_ELITE_NORMAL ||
                !Helper::IsGrindingLevelSuitable(bot->GetLevel(), creature->GetLevel(),
                    _minLevelOffset, _maxLevelOffset))
                return false;
        }

        // Do not steal another player's pull or deliberately enter an
        // unrelated fight. Attackers already engaged with this bot remain OK.
        if (Unit* victim = creature->GetVictim(); victim && victim != bot)
            return false;
        if (Player* recipient = creature->GetLootRecipient(); recipient && recipient != bot)
            return false;

        return true;
    }

    Creature* GrindAction::SelectTarget(Player* bot, const Blackboard::BotBlackboard& blackboard) const
    {
        Creature* bestTarget = nullptr;
        float bestScore = std::numeric_limits<float>::max();
        for (ObjectGuid guid : blackboard.spatial.hostileGuids)
        {
            Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (!IsSafeTarget(bot, creature))
                continue;

            // Prefer the safest useful enemy first, then distance. This keeps
            // the fallback conservative while still awarding non-trivial XP.
            float levelPenalty = static_cast<float>(creature->GetLevel()) * 100.0f;
            float score = levelPenalty + bot->GetDistance(creature);
            if (score < bestScore)
            {
                bestScore = score;
                bestTarget = creature;
            }
        }
        return bestTarget;
    }

    void GrindAction::TravelToHuntingGround(Player* bot, MovementManager* movement)
    {
        if (!bot || !movement || movement->GetState() != BotMovementState::Idle)
            return;

        Cache::PositionInfo destination;
        uint32_t creatureEntry = 0;
        if (!Cache::BotCache::FindNearestGrindingCreature(bot,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
            40.0f, _minLevelOffset, _maxLevelOffset, destination, creatureEntry))
            return;

        float targetZ = destination.z;
        if (Map* map = bot->GetMap())
        {
            float floorZ = map->GetHeight(bot->GetPhaseMask(), destination.x, destination.y,
                destination.z, true, 50.0f);
            if (floorZ > -500.0f && !std::isnan(floorZ))
                targetZ = floorZ;
        }

        if (Diagnostics::BotTrace::ShouldLog(bot))
        {
            float distance = Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                destination.x, destination.y);
            TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' relocating toward safe hunting entry {} at ({:.1f}, {:.1f}, {:.1f}) [{:.0f}yd away]",
                bot->GetName(), creatureEntry, destination.x, destination.y, targetZ, distance);
        }

        movement->MoveTo(destination.x, destination.y, targetZ, BotMovementState::Moving, false);
        _destinationRefreshMs = 15000;
    }

    void GrindAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!bot || !movement || !bot->IsInWorld() || !bot->IsAlive())
            return;

        _targetSearchCooldownMs = deltaMs >= _targetSearchCooldownMs ? 0 : _targetSearchCooldownMs - deltaMs;
        _destinationRefreshMs = deltaMs >= _destinationRefreshMs ? 0 : _destinationRefreshMs - deltaMs;

        Creature* target = _targetGuid ? ObjectAccessor::GetCreature(*bot, _targetGuid) : nullptr;
        if (!IsSafeTarget(bot, target))
        {
            if (target && !target->IsAlive() && bot->GetVictim() == target)
                bot->AttackStop();
            _targetGuid.Clear();
            target = nullptr;
        }

        if (!target && _targetSearchCooldownMs == 0)
        {
            _targetSearchCooldownMs = 500;
            target = SelectTarget(bot, blackboard);
            if (target)
            {
                _targetGuid = target->GetGUID();
                movement->Stop();
                if (Diagnostics::BotTrace::ShouldLog(bot))
                    TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' hunting {} (Entry {}, Level {})",
                        bot->GetName(), target->GetName(), target->GetEntry(), target->GetLevel());
            }
        }

        if (target)
        {
            if (!_classStrategy)
                _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
            if (_classStrategy)
                _classStrategy->UpdateCombat(bot, target, movement, blackboard, deltaMs);
            return;
        }

        if (_destinationRefreshMs == 0)
            TravelToHuntingGround(bot, movement);
    }

    void GrindAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot)
            bot->AttackStop();
        if (movement)
            movement->Stop();
        _targetGuid.Clear();
    }
}
