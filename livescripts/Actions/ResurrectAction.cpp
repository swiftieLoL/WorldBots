#include "ResurrectAction.h"
#include "Helper/TeleportUtils.h"
#include "Log.h"

namespace Actions
{
    ResurrectAction::ResurrectAction()
        : _completed(false)
    {
    }

    void ResurrectAction::Start(Player* bot, MovementManager* movement)
    {
        if (!bot || !bot->IsInWorld())
        {
            _completed = true;
            return;
        }

        TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) dead/ghost state detected! Executing graveyard repop & spirit resurrection...",
            bot->GetName(), bot->GetGUID().GetCounter());

        if (!bot->IsAlive())
        {
            bot->RepopAtGraveyard();
            Helper::TeleportUtils::CompletePendingTeleport(bot);
        }

        bot->ResurrectPlayer(0.5f, false);
        bot->SpawnCorpseBones();
        bot->CombatStop(true);
        bot->ClearInCombat();

        if (movement)
        {
            movement->Stop();
        }

        _completed = true;
    }

    void ResurrectAction::Update(Player* /*bot*/, MovementManager* /*movement*/, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/)
    {
        _completed = true;
    }

    void ResurrectAction::Stop(Player* /*bot*/, MovementManager* /*movement*/)
    {
    }
}
