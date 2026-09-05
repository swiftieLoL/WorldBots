#include "CombatAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Combat/ClassStrategies/ClassStrategyFactory.h"
#include "Combat/CombatEngagementPolicy.h"
#include "Party/PartyCombat.h"
#include "Helper/CombatUtils.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/TeleportUtils.h"
#include "ObjectAccessor.h"
#include "Log.h"
#include <limits>
#include <string>

namespace Actions
{
    CombatAction::CombatAction(ObjectGuid targetGuid)
        : _targetGuid(targetGuid)
    {
    }

    void CombatAction::Start(Player* bot, MovementManager* movement)
    {
        _progressWatchdog.Reset();
        _stallRecovery.Reset();
        _nonEngagementMs = 0;
        ResetOutcome();
        if (!bot || !bot->IsInWorld() || !_targetGuid)
        {
            Finish(ActionOutcome::RetryableFailure, "combat context was unavailable",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        Unit* target = ObjectAccessor::GetUnit(*bot, _targetGuid);
        if (!target || !target->IsAlive())
        {
            Finish(target ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure,
                target ? "combat target was already defeated" : "combat target disappeared",
                target ? FailureCategory::None : FailureCategory::Transient,
                target ? RecoveryDirective::None : RecoveryDirective::RetryLater);
            return;
        }

        _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
        if (!_classStrategy)
        {
            Finish(ActionOutcome::Blocked, "no combat strategy exists for this class",
                FailureCategory::ContentUnsupported, RecoveryDirective::None);
            return;
        }
    }

    bool CombatAction::TryUpdateContext(Player* bot, const Blackboard::BotBlackboard& bb)
    {
        if (!_targetGuid.IsEmpty() && bot)
        {
            Unit* target = ObjectAccessor::GetUnit(*bot, _targetGuid);
            if (target && target->IsAlive() && target->IsInWorld())
                return true;
        }

        if (!bb.combat.currentTargetGuid.IsEmpty() && bb.combat.currentTargetGuid != _targetGuid)
        {
            _targetGuid = bb.combat.currentTargetGuid;
            _progressWatchdog.Reset();
            _stallRecovery.Reset();
            return true;
        }

        return false;
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
            bool succeeded = target && !target->IsAlive();
            Finish(succeeded ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure,
                succeeded ? "combat target defeated" : "combat target became unavailable",
                succeeded ? FailureCategory::None : FailureCategory::Transient,
                succeeded ? RecoveryDirective::None : RecoveryDirective::RetryLater);
            return;
        }

        Creature* creatureTarget = target->ToCreature();
        Helper::CombatUtils::TargetValidationResult validation =
            Helper::CombatUtils::ValidateTarget(bot, target);
        if (validation != Helper::CombatUtils::TargetValidationResult::Valid)
        {
            bot->AttackStop();
            if (movement)
                movement->Stop();
            FailureCategory category = (validation == Helper::CombatUtils::TargetValidationResult::Evading)
                ? FailureCategory::Stalled
                : FailureCategory::Transient;
            RecoveryDirective directive = (validation == Helper::CombatUtils::TargetValidationResult::Evading)
                ? RecoveryDirective::Replan
                : RecoveryDirective::RetryLater;
            Finish(ActionOutcome::RetryableFailure,
                std::string("combat target failed live validation: ") +
                    Helper::CombatUtils::TargetValidationResultName(validation),
                category, directive);
            return;
        }

        bool targetEngagedWithBot = creatureTarget &&
            (creatureTarget->GetVictim() == bot ||
             creatureTarget->IsInCombatWith(bot));
        bool engagementProgress = Combat::HasEngagementProgress(
            bot->IsInCombat(), targetEngagedWithBot,
            movement && movement->GetState() != BotMovementState::Idle,
            movement && movement->HasPath(),
            bot->IsNonMeleeSpellCast(false));
        if (engagementProgress)
        {
            _nonEngagementMs = 0;
        }
        else
        {
            _nonEngagementMs = deltaMs >
                    std::numeric_limits<uint32_t>::max() - _nonEngagementMs
                ? std::numeric_limits<uint32_t>::max()
                : _nonEngagementMs + deltaMs;
            if (_nonEngagementMs >= Combat::NonEngagementTimeoutMs)
            {
                if (movement)
                    movement->Stop();
                Finish(ActionOutcome::RetryableFailure,
                    "combat target remained non-engaged with no executable movement or cast for 15 seconds",
                    FailureCategory::Navigation,
                    RecoveryDirective::Replan);
                return;
            }
        }

        if (!bot->IsInCombat() || !creatureTarget)
        {
            _progressWatchdog.Reset();
        }
        else if (_progressWatchdog.Update(bot->GetHealth(), creatureTarget->GetHealth(), deltaMs))
        {
            if (_stallRecovery.ShouldEscalateToSafeHub())
            {
                if (movement)
                    movement->Stop();
                Finish(ActionOutcome::RetryableFailure,
                    "combat target made no damage progress after two exact-target recoveries",
                    FailureCategory::Stalled, RecoveryDirective::Replan);
                return;
            }
            RecoverFromNoDamageStall(bot, creatureTarget, movement);
            return;
        }

        if (!_classStrategy)
        {
            _classStrategy = Combat::ClassStrategyFactory::GetStrategyForClass(bot->GetClass());
        }

        if (_classStrategy)
        {
            if (blackboard.self.isCCed)
            {
                if (movement)
                    movement->Stop();
                return; // Skip voluntary combat actions while hard crowd-controlled
            }
            if (blackboard.self.isRooted && movement)
            {
                movement->Stop();
            }
            if (Party::HandleRoleAction(bot, target, movement, blackboard))
                return;
            _classStrategy->UpdateCombat(bot, target, movement, blackboard, deltaMs);
        }
        else
        {
            Finish(ActionOutcome::Blocked, "no combat strategy exists for this class",
                FailureCategory::ContentUnsupported, RecoveryDirective::None);
        }
    }

    void CombatAction::RecoverFromNoDamageStall(Player* bot, Creature* target,
        MovementManager* movement)
    {
        if (!bot || !target || !bot->IsInWorld() || !target->IsInWorld() ||
            target->GetMap() != bot->GetMap())
            return;

        float targetX = target->GetPositionX();
        float targetY = target->GetPositionY();
        float targetZ = target->GetPositionZ();
        float facing = bot->GetAbsoluteAngle(target);
        ObjectGuid targetGuid = target->GetGUID();
        uint32 targetEntry = target->GetEntry();

        if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
        {
            TC_LOG_WARN("server", "[WorldBots] [Combat] Bot '{}' (GUID: {}) made no damage progress for {} seconds against NPC '{}' (Entry: {}, GUID: {}); relocating exactly to the NPC at ({:.2f}, {:.2f}, {:.2f})",
                bot->GetName(), bot->GetGUID().GetCounter(),
                Helper::CombatProgressWatchdog::StallTimeoutMs / 1000,
                target->GetName(), targetEntry, targetGuid.GetCounter(),
                targetX, targetY, targetZ);
        }

        if (movement)
            movement->Stop();

        bot->NearTeleportTo(targetX, targetY, targetZ, facing);
        if (!Helper::TeleportUtils::CompletePendingTeleport(bot))
        {
            if (Diagnostics::BotTrace::ShouldLog(bot, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_ERROR("server", "[WorldBots] [Combat] Bot '{}' could not complete its exact combat-stall relocation to NPC Entry {}",
                    bot->GetName(), targetEntry);
            }
            return;
        }

        Unit* refreshedTarget = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (refreshedTarget && refreshedTarget->IsAlive() && refreshedTarget->IsInWorld())
        {
            bot->SetInFront(refreshedTarget);
            bot->Attack(refreshedTarget, true);
        }
    }

    void CombatAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot)
        {
            bot->AttackStop();
            if (bot->IsNonMeleeSpellCast(false))
                bot->InterruptNonMeleeSpells(true);
        }
        if (movement)
        {
            movement->Stop();
        }
    }
}
