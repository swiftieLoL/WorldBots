#include "RevivePartyMemberAction.h"
#include "Corpse.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"

#include "Helper/SpellUtils.h"
#include "ObjectAccessor.h"

namespace Actions
{
    uint32_t RevivePartyMemberAction::GetResurrectionSpell(Player* bot) const
    {
        if (!bot)
            return 0;
        uint32_t baseSpell = Helper::SpellUtils::GetClassResurrectionSpell(bot->GetClass());
        return baseSpell ? Helper::SpellUtils::FindReadyRank(bot, baseSpell) : 0;
    }

    void RevivePartyMemberAction::Start(Player* /*bot*/, MovementManager* /*movement*/)
    {
        ResetOutcome();
        _elapsedMs = 0;
        _castAttemptElapsedMs = 0;
        _castStarted = false;
        if (!_targetGuid)
            Finish(ActionOutcome::Blocked, "no dead party member was selected", FailureCategory::Transient, RecoveryDirective::RetryLater);
    }

    void RevivePartyMemberAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& /*blackboard*/, uint32_t deltaMs)
    {
        if (_completed)
            return;
        if (!bot || !bot->IsAlive())
        {
            Finish(ActionOutcome::RetryableFailure, "resurrector became unavailable");
            return;
        }
        if (bot->IsInCombat())
        {
            Finish(ActionOutcome::RetryableFailure, "combat interrupted the party resurrection");
            return;
        }
        _elapsedMs += deltaMs;
        if (_castStarted)
            _castAttemptElapsedMs += deltaMs;
        Player* member = ObjectAccessor::FindPlayer(_targetGuid);
        if (!member || member->GetMap() != bot->GetMap())
        {
            Finish(ActionOutcome::RetryableFailure, "dead party member is no longer reachable on this map");
            return;
        }
        if (member->IsAlive())
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }
        if (member->IsResurrectRequestedBy(bot->GetGUID()))
        {
            Finish(ActionOutcome::Succeeded);
            return;
        }
        if (_elapsedMs >= 35000)
        {
            Finish(ActionOutcome::RetryableFailure, "party resurrection timed out before creating a request");
            return;
        }

        Corpse* corpse = member->GetCorpse();
        if (member->getDeathState() == DEAD && (!corpse || corpse->GetMapId() != bot->GetMapId()))
        {
            Finish(ActionOutcome::RetryableFailure, "party member released spirit and corpse is unavailable");
            return;
        }

        WorldObject* targetObj = (member->getDeathState() == DEAD && corpse)
            ? static_cast<WorldObject*>(corpse)
            : static_cast<WorldObject*>(member);

        if (bot->GetDistance(targetObj) > 28.0f || !bot->IsWithinLOSInMap(targetObj))
        {
            if (!_castStarted && movement)
                movement->MoveTo(targetObj->GetPositionX(), targetObj->GetPositionY(), targetObj->GetPositionZ(),
                    BotMovementState::Moving, false);
            return;
        }
        if (movement)
            movement->Stop();
        if (_castStarted && bot->IsNonMeleeSpellCast(false))
            return;

        // A cast that ended without creating a resurrection request was
        // interrupted or rejected. Release the latch and retry while the
        // overall action timeout still has budget.
        if (_castStarted && _castAttemptElapsedMs >= 250)
        {
            _castStarted = false;
            _castAttemptElapsedMs = 0;
        }
        if (_castStarted || bot->IsNonMeleeSpellCast(false))
            return;

        uint32_t spellId = GetResurrectionSpell(bot);
        if (!spellId)
        {
            // A known resurrection spell can be temporarily unavailable due
            // to cooldown or resource state. Keep the action alive and retry.
            return;
        }

        if (member->getDeathState() == DEAD && corpse)
            _castStarted = Helper::SpellUtils::TryCastCorpse(bot, corpse, spellId);
        else
            _castStarted = Helper::SpellUtils::TryCast(bot, member, spellId);

        if (_castStarted)
            _castAttemptElapsedMs = 0;
    }

    void RevivePartyMemberAction::Stop(Player* bot, MovementManager* movement)
    {
        if (bot && bot->IsNonMeleeSpellCast(false))
            bot->InterruptNonMeleeSpells(true);
        if (movement)
            movement->Stop();
    }
}
