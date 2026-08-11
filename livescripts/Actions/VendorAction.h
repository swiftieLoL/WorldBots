#pragma once

#include "BotAction.h"
#include "ObjectGuid.h"
#include "Blackboard/BotBlackboard.h"

class Creature;

namespace Actions
{
    class VendorAction : public BotAction
    {
    public:
        explicit VendorAction(uint32_t targetFreeSlots = 0, bool requireRepair = false,
            bool requireInventoryProgress = true)
            : _targetFreeSlots(targetFreeSlots), _requireRepair(requireRepair),
              _requireInventoryProgress(requireInventoryProgress) { }

        const char* GetName() const override { return "VendorAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsComplete() const override { return _completed; }
        bool IsInterruptible() const override { return _completed; }
        ActionOutcome GetOutcome() const override { return _outcome; }
        uint32_t GetRelatedNpcEntry() const override { return _relatedNpcEntry; }
        bool IsInventoryCapacityFailure() const override { return _inventoryCapacityFailure; }
        const std::string& GetOutcomeReason() const override { return _outcomeReason; }

    private:
        void AutoEquipUpgrades(Player* bot);
        void SellItems(Player* bot, Creature* vendor, uint32_t totalBagSlots);
        void RepairAll(Player* bot, Creature* vendor);
        void ExecuteTransaction(Player* bot, Creature* vendor, uint32_t totalBagSlots, bool logInventory);

        uint32_t _targetFreeSlots = 0;
        bool _requireRepair = false;
        bool _requireInventoryProgress = true;
        bool _started = false;
        bool _completed = false;
        bool _inventoryCapacityFailure = false;
        ActionOutcome _outcome = ActionOutcome::Running;
        uint32_t _relatedNpcEntry = 0;
        std::string _outcomeReason;
        Common::FailsafeTimer _failsafe;
    };
}
