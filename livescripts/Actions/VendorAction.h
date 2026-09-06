#pragma once

#include "BaseBotAction.h"
#include "ObjectGuid.h"
#include "Blackboard/BotBlackboard.h"
#include "Helper/VendorSelectionPolicy.h"
#include <unordered_map>
#include <limits>

class Creature;

namespace Actions
{
    class VendorAction : public BaseBotAction
    {
    public:
        explicit VendorAction(uint32_t targetFreeSlots = 0, bool requireRepair = false,
            bool requireInventoryProgress = true, bool requireRestock = false,
            std::unordered_map<uint32_t, uint32_t> suppressedNpcEntries = {},
            float maxTravelDistance = std::numeric_limits<float>::max(),
            bool partyVisit = false, ObjectGuid partyMemberGuid = ObjectGuid::Empty)
            : _targetFreeSlots(targetFreeSlots), _requireRepair(requireRepair),
              _requireInventoryProgress(requireInventoryProgress), _requireRestock(requireRestock),
              _suppressedNpcEntries(std::move(suppressedNpcEntries)),
              _maxTravelDistance(maxTravelDistance), _partyVisit(partyVisit),
              _partyMemberGuid(partyMemberGuid) { }

        const char* GetName() const override { return "VendorAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsInterruptible() const override { return _completed; }
        FailureCategory GetFailureCategory() const override;
        RecoveryDirective GetRecoveryDirective() const override;
        uint32_t GetRelatedNpcEntry() const override { return _relatedNpcEntry; }
        bool IsInventoryCapacityFailure() const override { return _inventoryCapacityFailure; }

    private:
        void AutoEquipUpgrades(Player* bot);
        bool RestockSupplies(Player* bot, Creature* vendor);
        void SellItems(Player* bot, Creature* vendor, uint32_t totalBagSlots);
        void RepairAll(Player* bot, Creature* vendor);
        void ExecuteTransaction(Player* bot, Creature* vendor, uint32_t totalBagSlots, bool logInventory);
        bool IsNpcSuppressed(uint32_t entry) const;

        uint32_t _targetFreeSlots = 0;
        bool _requireRepair = false;
        bool _requireInventoryProgress = true;
        bool _requireRestock = false;
        bool _inventoryCapacityFailure = false;
        uint32_t _relatedNpcEntry = 0;
        Common::FailsafeTimer _failsafe;
        uint32_t _travelLogCooldownMs = 0;
        bool _hasCachedTarget = false;
        uint32_t _cachedTargetMapId = 0;
        float _cachedTargetX = 0.0f;
        float _cachedTargetY = 0.0f;
        float _cachedTargetZ = 0.0f;
        std::unordered_map<uint32_t, uint32_t> _suppressedNpcEntries;
        float _maxTravelDistance = std::numeric_limits<float>::max();
        uint32_t _unreachableVendorCount = 0;
        bool _partyVisit = false;
        ObjectGuid _partyMemberGuid;
    };
}
