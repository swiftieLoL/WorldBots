#pragma once

#include "BaseBotAction.h"
#include "ObjectGuid.h"
#include "Helper/CommonTypes.h"
#include "Helper/MovementPathPolicy.h"
#include <cstddef>
#include <set>

namespace Actions
{
    class LootAction : public BaseBotAction
    {
    public:
        LootAction(ObjectGuid corpseGuid = ObjectGuid::Empty);

        const char* GetName() const override { return "LootAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsInterruptible() const override { return _completed; }
        ObjectGuid GetRelatedTargetGuid() const override { return _targetGuid; }

        static void ClearBotState(ObjectGuid botGuid);
        static void ClearAllState();
        static void SuppressTarget(Player* bot, ObjectGuid targetGuid, uint32_t durationSec = 300);
        static void SuppressDangerousArea(Player* bot, ObjectGuid anchorGuid,
            float radius = 60.0f, uint32_t durationSec = 300);

        static bool HasLootableTargets(Player* bot, const std::set<uint64_t>& ignoredGuids = {});
        static bool HasInventoryBlockedLoot(Player* bot);
        static std::size_t GetInventoryBlockedLootCount(Player* bot);

    private:
        ObjectGuid FindLootTarget(Player* bot);
        bool PerformLoot(Player* bot);

        ObjectGuid _targetGuid;
        bool _channelStarted = false;
        uint32_t _channelTimerMs = 0;
        Common::FailsafeTimer _failsafe;
        Helper::MovementPathPolicy::WaypointProgressTracker _travelProgress;
    };
}
