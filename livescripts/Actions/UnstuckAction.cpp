#include "UnstuckAction.h"
#include "Helper/TeleportUtils.h"
#include "Log.h"

namespace Actions
{
    UnstuckAction::UnstuckAction(uint32_t deadlyQuestId)
        : _deadlyQuestId(deadlyQuestId), _completed(false)
    {
    }

    void UnstuckAction::Start(Player* bot, MovementManager* movement)
    {
        if (!bot || !bot->IsInWorld())
        {
            _completed = true;
            return;
        }

        ObjectGuid guid = bot->GetGUID();

        TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) BRUTE-FORCE UNSTUCK: Relocating to Homebind, saving DB, and re-logging session...",
            bot->GetName(), guid.GetCounter());

        // 1. Abandon deadly quest in TrinityCore QuestLog to prevent re-populating in Blackboard
        if (_deadlyQuestId != 0 && bot->GetQuestStatus(_deadlyQuestId) != QUEST_STATUS_NONE)
        {
            bot->AbandonQuest(_deadlyQuestId);
            TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' ABANDONED deadly quest {}!",
                bot->GetName(), _deadlyQuestId);
        }

        // 2. Stop combat and clear threat
        bot->CombatStop(true);
        bot->ClearInCombat();

        // 3. This path is only reached after repeated deaths. Restore a viable
        // combat state as well as position; otherwise broken equipment sends
        // the bot straight back into the same death loop after teleporting.
        bot->ResurrectPlayer(1.0f, false);
        bot->SpawnCorpseBones();
        bot->DurabilityRepairAll(false, 1.0f, false);
        bot->SetHealth(bot->GetMaxHealth());

        // 4. Relocate to Homebind or Start Position
        if (bot->m_homebindX != 0.0f)
        {
            bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ, bot->GetOrientation());
        }
        else
        {
            bot->TeleportTo(bot->GetStartPosition());
        }
        Helper::TeleportUtils::CompletePendingTeleport(bot);

        if (movement)
        {
            movement->Stop();
        }

        // 5. Save state to character DB
        bot->SaveToDB();

        // Keep the existing session alive. Lifecycle ownership belongs to the
        // central runtime; actions must not create competing login pipelines.
        _completed = true;
    }

    void UnstuckAction::Update(Player* /*bot*/, MovementManager* /*movement*/, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t /*deltaMs*/)
    {
        _completed = true;
    }

    void UnstuckAction::Stop(Player* /*bot*/, MovementManager* /*movement*/)
    {
    }
}
