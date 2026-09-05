#include "ResurrectAction.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Helper/TeleportUtils.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include <list>

namespace
{
    bool IsUnsafeGraveyard(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() || !bot->GetMap())
            return false;

        constexpr float GraveyardSafetyScanRadius = 40.0f;
        std::list<Unit*> units;
        Trinity::AnyUnitInObjectRangeCheck check(bot, GraveyardSafetyScanRadius);
        Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
        Cell::VisitGridObjects(bot, searcher, GraveyardSafetyScanRadius);

        for (Unit* unit : units)
        {
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (!creature || !creature->IsAlive() || creature->IsCritter() ||
                creature->IsCivilian() || !creature->isTargetableForAttack())
                continue;
            if (creature->CanStartAttack(bot, false))
                return true;
        }
        return false;
    }

    bool RelocateToBind(Player* bot)
    {
        if (!bot)
            return false;
        Helper::TeleportUtils::TeleportToHomebind(bot);
        return !bot->IsBeingTeleported();
    }
}

namespace Actions
{
    ResurrectAction::ResurrectAction()
    {
    }

    void ResurrectAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        if (!bot || !bot->IsInWorld())
        {
            Finish(ActionOutcome::RetryableFailure, "resurrection context was unavailable",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) dead/ghost state detected! Executing graveyard repop & spirit resurrection...",
            bot->GetName(), bot->GetGUID().GetCounter());

        if (!bot->IsAlive())
        {
            bot->RepopAtGraveyard();
            if (!Helper::TeleportUtils::CompletePendingTeleport(bot))
            {
                Finish(ActionOutcome::Running, "graveyard teleport transfer in progress");
                return;
            }
        }

        bot->ResurrectPlayer(0.5f, false);
        bot->SpawnCorpseBones();
        bot->CombatStop(true);
        bot->ClearInCombat();

        // Some custom graveyards overlap normal hostile spawn pockets. In
        // that case the half-health bot is attacked before target suppression
        // can steer it elsewhere, recreating the same death loop. Evacuate
        // only when a live creature could aggro immediately after resurrection.
        if (IsUnsafeGraveyard(bot))
        {
            TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' resurrected inside a hostile aggro range; relocating to its bind point before resuming normal goals",
                bot->GetName());
            bot->CombatStop(true);
            bot->ClearInCombat();
            if (RelocateToBind(bot))
                bot->SetHealth(bot->GetMaxHealth());
        }

        if (movement)
        {
            movement->Stop();
        }

        bool originValid = bot->IsAlive() &&
            Helper::TeleportUtils::HasUsableGroundOrigin(bot);
        bool locallyCorrected = false;
        bool returnedToBind = false;
        if (bot->IsAlive() && !originValid)
        {
            locallyCorrected =
                Helper::TeleportUtils::TryRelocateToLocalNavmesh(bot);
            originValid = locallyCorrected;
            if (!originValid)
            {
                TC_LOG_WARN("server", "[WorldBots] [Recovery] Bot '{}' resurrected at an origin that could not start a complete ground path; relocating to its bind point before resuming goals",
                    bot->GetName());
                returnedToBind = RelocateToBind(bot);
                originValid = returnedToBind &&
                    Helper::TeleportUtils::HasUsableGroundOrigin(bot);
            }
            if (movement)
                movement->ResetOriginPathRecovery();
        }

        bool succeeded = bot->IsAlive() && originValid;
        Finish(succeeded ? ActionOutcome::Succeeded : ActionOutcome::RetryableFailure,
            !bot->IsAlive() ? "resurrection did not restore the bot" :
            (locallyCorrected ? "resurrection completed after local navmesh correction" :
                (returnedToBind && originValid
                    ? "resurrection completed at a validated bind origin"
                    : (originValid ? "resurrection completed" :
                        "resurrection completed but no usable ground origin was found"))),
            succeeded ? FailureCategory::None :
                (bot->IsAlive() ? FailureCategory::Navigation : FailureCategory::Interaction),
            succeeded ? RecoveryDirective::None :
                (bot->IsAlive() ? RecoveryDirective::Replan : RecoveryDirective::RetryLater));
    }

    void ResurrectAction::Update(Player* /*bot*/, MovementManager* /*movement*/, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/)
    {
        if (!_completed)
        {
            Finish(ActionOutcome::RetryableFailure, "resurrection ended before it was attempted",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
        }
    }

    void ResurrectAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot && bot->IsNonMeleeSpellCast(false))
            bot->InterruptNonMeleeSpells(true);
        if (movement)
            movement->Stop();
    }
}
