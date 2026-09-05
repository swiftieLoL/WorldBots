#include "TownRunAction.h"
#include "TownRunOutcomePolicy.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Entities/Creature/Creature.h"
#include "Helper/TeleportUtils.h"
#include "Helper/NpcFinder.h"
#include "Diagnostics/BotTrace.h"
#include "Diagnostics/StructuredEventLog.h"
#include "Server/WorldSession.h"

#include "QuestAction.h"
#include "VendorAction.h"
#include "Party/PartyRecruitmentPolicy.h"

namespace Actions
{
    namespace
    {
        void DiscoverTownServices(Player* bot)
        {
            if (!bot || !bot->IsInWorld())
                return;

            for (Creature* creature : Helper::NpcUtils::FindNearbyFriendlyCreatures(bot, 40.0f))
            {

                if (creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_FLIGHTMASTER))
                {
                    if (WorldSession* session = bot->GetSession())
                    {
                        session->SendLearnNewTaxiNode(creature);
                        if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
                        {
                            Diagnostics::StructuredEvent event;
                            event.event = "flight_master_discovered";
                            event.goal = "TownRun";
                            event.action = "TownRunAction";
                            event.requestX = creature->GetPositionX();
                            event.requestY = creature->GetPositionY();
                            event.requestZ = creature->GetPositionZ();
                            event.details = "entry=" + std::to_string(creature->GetEntry());
                            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                        }
                        Diagnostics::BotTrace::LogToFile(bot, "Town",
                            "Discovered Flight Master entry " + std::to_string(creature->GetEntry()) +
                            " (" + creature->GetName() + ")", Diagnostics::LogEvent::Normal);
                    }
                }

                if (creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_INNKEEPER))
                {
                    if (Helper::TeleportUtils::ShouldUpdateHomebind(bot, creature))
                    {
                        uint32 oldMap = bot->m_homebindMapId;
                        uint32 oldArea = bot->m_homebindAreaId;
                        Helper::TeleportUtils::SetHomebind(bot, creature->GetMapId(),
                            creature->GetAreaId(), creature->GetPositionX(),
                            creature->GetPositionY(), creature->GetPositionZ());
                        if (Diagnostics::StructuredEventLog::ShouldCapture(bot))
                        {
                            Diagnostics::StructuredEvent event;
                            event.event = "innkeeper_bound";
                            event.goal = "TownRun";
                            event.action = "TownRunAction";
                            event.requestX = creature->GetPositionX();
                            event.requestY = creature->GetPositionY();
                            event.requestZ = creature->GetPositionZ();
                            event.outcome = "Succeeded";
                            event.details = "old_map=" + std::to_string(oldMap) +
                                ";old_area=" + std::to_string(oldArea) +
                                ";new_map=" + std::to_string(creature->GetMapId()) +
                                ";new_area=" + std::to_string(creature->GetAreaId()) +
                                ";entry=" + std::to_string(creature->GetEntry());
                            Diagnostics::StructuredEventLog::Write(bot, std::move(event));
                        }
                        Diagnostics::BotTrace::LogToFile(bot, "Town",
                            "Updated homebind to Innkeeper entry " + std::to_string(creature->GetEntry()) +
                            " (" + creature->GetName() + ") Area " + std::to_string(creature->GetAreaId()),
                            Diagnostics::LogEvent::Important);
                    }
                }
            }
        }
    }

    void TownRunAction::Start(Player* bot, MovementManager* movement)
    {
        _nextStep = 0;
        StopChild(bot, movement);
        ResetOutcome();
        _inventoryCapacityFailure = false;
        _failedDuringWorldTravel = false;
        _hasTravelFailureArea = false;
        _travelFailureArea = {};
        _relatedQuestId = 0;
        _relatedNpcEntry = 0;

        if (!bot || !bot->IsInWorld() || !movement)
        {
            Finish(ActionOutcome::RetryableFailure, "bot or movement manager was unavailable when the town run started",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        if (_plan.Empty())
        {
            TownRunTerminalOutcome terminal = ResolveTownRunTerminalOutcome(
                _plan.blockedByMissingVendor, _plan.blockedByProtectedInventory);
            _inventoryCapacityFailure = terminal.inventoryCapacityFailure;
            Finish(terminal.outcome,
                _plan.blockedByMissingVendor ? "town services require a vendor, but no usable vendor is known" :
                (_plan.blockedByProtectedInventory ? "quest reward space cannot be created without destroying protected items" : std::string{}),
                terminal.failureCategory, terminal.recoveryDirective);
            return;
        }

        DiscoverTownServices(bot);
        StartNextStep(bot, movement);
    }

    void TownRunAction::StartNextStep(Player* bot, MovementManager* movement)
    {
        if (_nextStep >= _plan.steps.size())
        {
            if (_plan.blockedByMissingVendor)
            {
                TownRunTerminalOutcome terminal = ResolveTownRunTerminalOutcome(true, false);
                Finish(ActionOutcome::Blocked,
                    "available town services completed, but another required vendor capability is missing",
                    terminal.failureCategory, terminal.recoveryDirective);
            }
            else
                Finish(ActionOutcome::Succeeded);
            return;
        }

        Town::Step step = _plan.steps[_nextStep];
        std::unique_ptr<BotAction> nextChild;
        if (step.service == Town::Service::TurnInQuest)
        {
            ++_nextStep;
            nextChild = std::make_unique<TurnInQuestAction>(step.questId, _dangerAreas);
        }
        else
        {
            Town::Service phase = step.service;
            bool requireRepair = phase == Town::Service::Repair;
            bool requireInventoryProgress = false;
            bool requireRestock = phase == Town::Service::Restock;
            uint32_t targetFreeSlots = 0;
            while (_nextStep < _plan.steps.size())
            {
                Town::Service current = _plan.steps[_nextStep].service;
                bool sameInventoryPhase =
                    (phase == Town::Service::Sell || phase == Town::Service::CreateRewardSpace) &&
                    (current == Town::Service::Sell || current == Town::Service::CreateRewardSpace);
                if (current == Town::Service::TurnInQuest ||
                    (!sameInventoryPhase && current != phase))
                    break;

                requireInventoryProgress = requireInventoryProgress ||
                    current == Town::Service::Sell || current == Town::Service::CreateRewardSpace;
                if (current == Town::Service::CreateRewardSpace)
                    targetFreeSlots = _plan.targetFreeBagSlots;
                ++_nextStep;
            }
            nextChild = std::make_unique<VendorAction>(targetFreeSlots, requireRepair,
                requireInventoryProgress, requireRestock, _suppressedNpcEntries,
                _maxVendorTravelDistance);
        }

        StartChild(std::move(nextChild), bot, movement);
    }

    void TownRunAction::Update(Player* bot, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (_completed || !_childAction)
            return;

        UpdateChild(bot, movement, blackboard, deltaMs);
        if (!_childAction->IsComplete())
            return;

        ActionOutcome childOutcome = _childAction->GetOutcome();
        _failedDuringWorldTravel = childOutcome != ActionOutcome::Succeeded &&
            _childAction->IsWorldTravelInProgress();
        _hasTravelFailureArea = _failedDuringWorldTravel &&
            _childAction->GetTravelFailureArea(_travelFailureArea);
        _relatedQuestId = _childAction->GetRelatedQuestId();
        _relatedNpcEntry = _childAction->GetRelatedNpcEntry();
        _inventoryCapacityFailure = _childAction->IsInventoryCapacityFailure();
        _failureCategory = _childAction->GetFailureCategory();
        _recoveryDirective = _childAction->GetRecoveryDirective();
        std::string childReason = _childAction->GetOutcomeReason();
        StopChild(bot, movement);

        if (childOutcome != ActionOutcome::Succeeded)
        {
            Finish(childOutcome, std::move(childReason), _failureCategory, _recoveryDirective);
            return;
        }

        if (_relatedQuestId != 0)
        {
            Party::PartyRecruitmentPolicy::CheckAndDisbandIfCompleted(bot, _relatedQuestId);
        }

        _relatedQuestId = 0;
        _relatedNpcEntry = 0;
        _inventoryCapacityFailure = false;
        _failureCategory = FailureCategory::None;
        _recoveryDirective = RecoveryDirective::None;
        DiscoverTownServices(bot);
        StartNextStep(bot, movement);
    }

    void TownRunAction::Stop(Player* bot, MovementManager* movement)
    {
        CompositeBotAction::Stop(bot, movement);
    }

    bool TownRunAction::IsInterruptible() const
    {
        return _completed || !_childAction || _childAction->IsInterruptible();
    }
}
