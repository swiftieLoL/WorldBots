#pragma once

#include "ObjectGuid.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Player.h"
#include "Brain/SuppressionRegistry.h"
#include "Brain/TravelHazardPolicy.h"
#include "Actions/BotAction.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/TimeUtils.h"
#include "Log.h"

namespace Brain
{
    class HostileContextTracker
    {
    public:
        void RememberHostileTarget(Player* bot, Actions::BotAction* activeAction, Brain::BotGoal goal)
        {
            if (!bot || !activeAction)
                return;

            ObjectGuid targetGuid = activeAction->GetRelatedTargetGuid();
            if (!targetGuid)
            {
                if (goal != Brain::BotGoal::Combat && goal != Brain::BotGoal::Flee && goal != Brain::BotGoal::Grind)
                    ClearHostile();
                return;
            }

            Creature* target = ObjectAccessor::GetCreature(*bot, targetGuid);
            if (!target)
            {
                if (goal != Brain::BotGoal::Combat && goal != Brain::BotGoal::Flee && goal != Brain::BotGoal::Grind)
                    ClearHostile();
                return;
            }

            _lastHostileTargetGuid = targetGuid;
            _lastHostileSpawnId = target->GetSpawnId();
            _lastHostileTargetEntry = target->GetEntry();
        }

        void ClearHostile()
        {
            _lastHostileTargetGuid.Clear();
            _lastHostileSpawnId = 0;
            _lastHostileTargetEntry = 0;
        }

        void SuppressFatalHostileTarget(Player* bot, SuppressionRegistry& suppressions, std::function<bool(uint32_t)> isRequiredByQuest)
        {
            if (!bot || !_lastHostileTargetGuid || _lastHostileSpawnId == 0 ||
                isRequiredByQuest(_lastHostileTargetEntry))
            {
                ClearHostile();
                return;
            }

            constexpr uint32_t FatalGrindSpawnSuppressionSeconds = 300;
            constexpr uint32_t FatalGrindEntrySuppressionSeconds = 900;
            uint32_t nowSec = Helper::MonotonicSeconds();

            suppressions.SuppressGrindSpawn(_lastHostileSpawnId, nowSec + FatalGrindSpawnSuppressionSeconds);
            suppressions.SuppressGrindEntry(_lastHostileTargetEntry, nowSec + FatalGrindEntrySuppressionSeconds);

            TC_LOG_INFO("server", "[WorldBots] [Grind] Bot '{}' suppressed fatal non-quest mob Entry {} for {} seconds and Spawn {} for {} seconds; future grind selection will choose a different creature type",
                bot->GetName(), _lastHostileTargetEntry, FatalGrindEntrySuppressionSeconds,
                _lastHostileSpawnId, FatalGrindSpawnSuppressionSeconds);

            ClearHostile();
        }

        void RememberProactiveRouteTarget(Actions::BotAction* activeAction,
            Brain::BotGoal goal)
        {
            if (!activeAction)
                return;

            bool townTravel = goal == Brain::BotGoal::TownRun ||
                goal == Brain::BotGoal::Vendor;
            bool questTravel = (goal == Brain::BotGoal::AcceptQuest ||
                goal == Brain::BotGoal::TurnInQuest ||
                goal == Brain::BotGoal::ProgressQuest) &&
                activeAction->IsWorldTravelInProgress();
            if (!townTravel && !questTravel)
                return;

            _proactiveRouteTarget.Observe(activeAction->GetRelatedQuestId(),
                activeAction->GetRelatedNpcEntry());
        }

        void ClearProactiveRoute()
        {
            _proactiveRouteTarget.Clear();
        }

        uint32_t SuppressFatalProactiveRoute(Player* bot,
            SuppressionRegistry& suppressions)
        {
            if (!bot || !_proactiveRouteTarget.HasTarget())
                return 0;
            uint32_t questId = _proactiveRouteTarget.GetQuestId();
            SuppressProactiveRoute(bot, suppressions, 0, true);
            ClearProactiveRoute();
            return questId;
        }

        uint32_t SuppressDangerousProactiveRoute(Player* bot,
            SuppressionRegistry& suppressions,
            uint32_t hostileEntry)
        {
            if (!bot || !_proactiveRouteTarget.HasTarget())
                return 0;
            uint32_t questId = _proactiveRouteTarget.GetQuestId();
            SuppressProactiveRoute(bot, suppressions, hostileEntry, false);
            ClearProactiveRoute();
            return questId;
        }

        uint32_t GetLastHostileTargetEntry() const { return _lastHostileTargetEntry; }
        bool HasProactiveRouteTarget() const { return _proactiveRouteTarget.HasTarget(); }

    private:
        void SuppressProactiveRoute(Player* bot, SuppressionRegistry& suppressions,
            uint32_t hostileEntry, bool fatal)
        {
            uint32_t nowSec = Helper::MonotonicSeconds();
            if (uint32_t questId = _proactiveRouteTarget.GetQuestId())
            {
                constexpr uint32_t UnsafeQuestRouteSuppressionSeconds = 900;
                suppressions.SuppressQuest(questId,
                    nowSec + UnsafeQuestRouteSuppressionSeconds);
                if (fatal)
                {
                    TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' died during world travel for quest {}; deferring that quest for {} seconds and replanning",
                        bot->GetName(), questId, UnsafeQuestRouteSuppressionSeconds);
                }
                else
                {
                    if (Diagnostics::BotTrace::ShouldLog(bot))
                    {
                        TC_LOG_WARN("server", "[WorldBots] [Quest] Bot '{}' encountered unsafe hostile Entry {} during world travel for quest {}; deferring that quest for {} seconds and escaping",
                            bot->GetName(), hostileEntry, questId,
                            UnsafeQuestRouteSuppressionSeconds);
                    }
                }
                return;
            }

            uint32_t npcEntry = _proactiveRouteTarget.GetNpcEntry();
            if (npcEntry == 0)
                return;
            constexpr uint32_t UnsafeServiceRouteSuppressionSeconds = 300;
            suppressions.SuppressNpc(npcEntry,
                nowSec + UnsafeServiceRouteSuppressionSeconds);
            if (fatal)
            {
                TC_LOG_WARN("server", "[WorldBots] [Town] Bot '{}' died while travelling to service NPC Entry {}; suppressing that route for {} seconds and replanning",
                    bot->GetName(), npcEntry, UnsafeServiceRouteSuppressionSeconds);
            }
            else
            {
                if (Diagnostics::BotTrace::ShouldLog(bot))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Town] Bot '{}' encountered unsafe hostile Entry {} while travelling to service NPC Entry {}; suppressing that route for {} seconds and escaping",
                        bot->GetName(), hostileEntry, npcEntry,
                        UnsafeServiceRouteSuppressionSeconds);
                }
            }
        }

        ObjectGuid _lastHostileTargetGuid;
        uint64_t _lastHostileSpawnId = 0;
        uint32_t _lastHostileTargetEntry = 0;
        ProactiveRouteTarget _proactiveRouteTarget;
    };
}
