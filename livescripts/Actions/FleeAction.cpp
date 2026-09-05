#include "FleeAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"

#include "ObjectAccessor.h"
#include "Creature.h"
#include "Blackboard/BotBlackboard.h"
#include "Brain/ThreatAssessmentPolicy.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "Helper/Constants.h"

namespace Actions
{
    FleeAction::FleeAction(ObjectGuid threatGuid)
        : _threatGuid(threatGuid)
    {
    }

    void FleeAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        _elapsedMs = 0;
        _combatClearMs = 0;
        _pathRetryMs = 0;
        _disengageCooldownMs = 0;
        _sawCombat = bot && bot->IsInCombat();
        if (!_classStrategy && bot)
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());

        if (!bot || !bot->IsInWorld() || !_threatGuid)
        {
            Finish(ActionOutcome::RetryableFailure, "escape context was unavailable",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        if (movement)
        {
            movement->BeginFleeRecovery();
            movement->Stop();
        }
    }

    void FleeAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (_completed)
            return;

        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "flee action context was unavailable",
                movement ? FailureCategory::Transient : FailureCategory::Navigation,
                RecoveryDirective::RetryLater);
            return;
        }

        if (!bot->IsAlive())
        {
            movement->Stop();
            Finish(ActionOutcome::Succeeded, "bot died during escape; resurrection owns recovery");
            return;
        }

        Unit* threat = ObjectAccessor::GetUnit(*bot, _threatGuid);
        if (!threat || !threat->IsInWorld() || !threat->IsAlive() || threat->GetMap() != bot->GetMap())
        {
            movement->Stop();
            Finish(ActionOutcome::Succeeded, "threat is no longer active");
            return;
        }

        if (Creature* cThreat = threat->ToCreature())
        {
            if (cThreat->IsInEvadeMode())
            {
                movement->Stop();
                Finish(ActionOutcome::Succeeded, "threat entered evade mode");
                return;
            }
        }

        if (_disengageCooldownMs > 0)
            _disengageCooldownMs = _disengageCooldownMs > deltaMs ? _disengageCooldownMs - deltaMs : 0;

        // "Pop & Run": If disengage is ready, find the closest active threat/attacker within 10 yards
        // and attempt class-defined CC/peel before or during the escape.
        if (_classStrategy && _disengageCooldownMs == 0)
        {
            Unit* peelTarget = threat;
            float closestDist = bot->GetDistance(threat);

            for (ObjectGuid attackerGuid : blackboard.combat.attackerGuids)
            {
                Unit* attacker = ObjectAccessor::GetUnit(*bot, attackerGuid);
                if (attacker && attacker->IsInWorld() && attacker->IsAlive() &&
                    attacker->GetMap() == bot->GetMap())
                {
                    float d = bot->GetDistance(attacker);
                    if (d < closestDist)
                    {
                        closestDist = d;
                        peelTarget = attacker;
                    }
                }
            }

            if (peelTarget && closestDist <= 10.0f)
            {
                if (_classStrategy->TryDisengageCC(bot, peelTarget, blackboard))
                {
                    _disengageCooldownMs = 4000;
                }
            }
        }

        bool hasActiveAttackers = false;
        for (ObjectGuid attackerGuid : blackboard.combat.attackerGuids)
        {
            Unit* attacker = ObjectAccessor::GetUnit(*bot, attackerGuid);
            if (attacker && attacker->IsInWorld() && attacker->IsAlive() &&
                attacker->GetMap() == bot->GetMap() &&
                (attacker->GetVictim() == bot || attacker->IsInCombatWith(bot)))
            {
                hasActiveAttackers = true;
                break;
            }
        }

        Brain::FleeCompletionInput completionInput;
        completionInput.botInCombat = bot->IsInCombat();
        completionInput.blackboardInCombat = blackboard.self.inCombat;
        completionInput.hasActiveAttackers = hasActiveAttackers;
        completionInput.threatEngaged = threat->GetVictim() == bot || threat->IsInCombatWith(bot);
        completionInput.threatSafelySeparated =
            bot->GetDistance(threat) > Constants::TacticalScanRadius;
        completionInput.movementIdle = movement->GetState() == BotMovementState::Idle;
        completionInput.combatClearMs = _combatClearMs;

        bool combatClear = Brain::IsFleeCombatClear(completionInput);
        if (combatClear)
            _combatClearMs += deltaMs;
        else
        {
            _combatClearMs = 0;
            _sawCombat = true;
        }
        completionInput.combatClearMs = _combatClearMs;

        // Distance alone is not a reliable escape condition: a higher-level
        // creature's aggro range can exceed the old fixed 25-yard threshold.
        // Require combat and its attacker observations to remain clear long
        // enough to bridge normal sense-update jitter before ending the flee.
        if (Brain::ShouldCompleteFleeAfterCombatDrop(completionInput))
        {
            movement->Stop();
            Finish(ActionOutcome::Succeeded, _sawCombat
                ? "combat dropped and remained clear"
                : "threat avoided without entering combat");
            return;
        }

        _elapsedMs += deltaMs;
        if (_elapsedMs >= 10000)
        {
            movement->Stop();
            Finish(ActionOutcome::Blocked, "escape timed out before combat cleared",
                FailureCategory::Navigation, RecoveryDirective::Replan);
            return;
        }

        // Keep the current escape leg stable until it completes. Recomputing
        // a point 15 yards ahead from the bot's new position every 50 ms makes
        // the destination drift continuously and repeatedly rebuilds paths.
        if (movement->GetState() == BotMovementState::Fleeing)
            return;

        _pathRetryMs = _pathRetryMs > deltaMs ? _pathRetryMs - deltaMs : 0;
        if (_pathRetryMs > 0)
            return;

        // MovementManager samples multiple complete navmesh paths away from
        // the threat and owns the exact smooth route it selected. Failed
        // samples are briefly backed off to avoid an expensive query every
        // brain tick while the combat scene is unchanged.
        if (!movement->MoveAwayFrom(threat, Constants::DefaultFleeStepDistance))
            _pathRetryMs = 500;
    }

    void FleeAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot && bot->IsNonMeleeSpellCast(false))
            bot->InterruptNonMeleeSpells(true);
        _classStrategy.reset();
        if (movement)
        {
            movement->Stop();
            movement->EndFleeRecovery();
        }
    }
}
