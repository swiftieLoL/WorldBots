#pragma once

#include "CompositeBotAction.h"
#include "Brain/SuppressionRegistry.h"
#include "Town/TownPlanning.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <limits>

namespace Actions
{
    class TownRunAction : public CompositeBotAction
    {
    public:
        explicit TownRunAction(Town::Plan plan,
            std::unordered_map<uint32_t, uint32_t> suppressedNpcEntries = {},
            float maxVendorTravelDistance = std::numeric_limits<float>::max(),
            std::vector<Brain::DangerArea> dangerAreas = {})
            : _plan(std::move(plan)), _suppressedNpcEntries(std::move(suppressedNpcEntries)),
              _maxVendorTravelDistance(maxVendorTravelDistance),
              _dangerAreas(std::move(dangerAreas)) { }

        const char* GetName() const override { return "TownRunAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;

        bool IsInterruptible() const override;
        uint32_t GetRelatedQuestId() const override
        {
            return _childAction ? _childAction->GetRelatedQuestId() : _relatedQuestId;
        }
        uint32_t GetRelatedNpcEntry() const override
        {
            return _childAction ? _childAction->GetRelatedNpcEntry() : _relatedNpcEntry;
        }
        bool IsInventoryCapacityFailure() const override { return _inventoryCapacityFailure; }
        bool IsWorldTravelInProgress() const override
        {
            return _failedDuringWorldTravel ||
                (_childAction && _childAction->IsWorldTravelInProgress());
        }
        bool GetTravelFailureArea(Brain::DangerArea& area) const override
        {
            if (_hasTravelFailureArea)
            {
                area = _travelFailureArea;
                return true;
            }
            return CompositeBotAction::GetTravelFailureArea(area);
        }

    private:
        void StartNextStep(Player* bot, MovementManager* movement);

        Town::Plan _plan;
        std::size_t _nextStep = 0;
        bool _inventoryCapacityFailure = false;
        bool _failedDuringWorldTravel = false;
        bool _hasTravelFailureArea = false;
        Brain::DangerArea _travelFailureArea;
        uint32_t _relatedQuestId = 0;
        uint32_t _relatedNpcEntry = 0;
        std::unordered_map<uint32_t, uint32_t> _suppressedNpcEntries;
        float _maxVendorTravelDistance = std::numeric_limits<float>::max();
        std::vector<Brain::DangerArea> _dangerAreas;
    };
}
