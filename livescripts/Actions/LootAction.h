#pragma once

#include "BotAction.h"
#include "ObjectGuid.h"
#include "Helper/CommonTypes.h"
#include <cstddef>
#include <set>

namespace Actions
{
    class LootAction : public BotAction
    {
    public:
        LootAction(ObjectGuid corpseGuid = ObjectGuid::Empty);

        const char* GetName() const override { return "LootAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override;
        bool IsInterruptible() const override { return _completed; }

        static bool HasLootableTargets(Player* bot, const std::set<uint64_t>& ignoredGuids = {});
        static bool HasInventoryBlockedLoot(Player* bot);
        static std::size_t GetInventoryBlockedLootCount(Player* bot);

    private:
        ObjectGuid FindLootTarget(Player* bot);
        bool PerformLoot(Player* bot);

        ObjectGuid _targetGuid;
        bool _started;
        bool _completed;
        bool _channelStarted = false;
        uint32_t _channelTimerMs = 0;
        Common::FailsafeTimer _failsafe;
    };
}
