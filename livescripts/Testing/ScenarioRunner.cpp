#include "ScenarioRunner.h"

#include <sstream>
#include <vector>

namespace Testing
{
    namespace
    {
        const char* ServiceName(Town::Service service)
        {
            switch (service)
            {
                case Town::Service::Sell: return "Sell";
                case Town::Service::Repair: return "Repair";
                case Town::Service::Restock: return "Restock";
                case Town::Service::CreateRewardSpace: return "CreateRewardSpace";
                case Town::Service::PartyVisit: return "PartyVisit";
                case Town::Service::TurnInQuest: return "TurnInQuest";
                default: return "Unknown";
            }
        }

        struct Check
        {
            const char* name;
            bool passed;
        };
    }

    std::string ScenarioRunner::ListScenarios()
    {
        return "WorldBots test commands:\n"
               " - .bot test logic: run deterministic town-planner scenarios in the realm process\n"
               " - .bot test plan [name]: preview the plan for a live bot without changing it";
    }

    std::string ScenarioRunner::RunLogicScenarios()
    {
        std::vector<Check> checks;

        Town::PlanningInput empty;
        empty.freeBagSlots = 10;
        empty.hasInventoryVendor = true;
        empty.hasRepairVendor = true;
        empty.hasRestockVendor = true;
        checks.push_back({ "no needs produces no plan", Town::BuildPlan(empty).Empty() });

        Town::PlanningInput combined;
        combined.freeBagSlots = 0;
        combined.hasSellableItems = true;
        combined.needsInventoryCleanup = true;
        combined.needsRepair = true;
        combined.hasInventoryVendor = true;
        combined.hasRepairVendor = true;
        combined.hasRestockVendor = true;
        combined.completedQuests = {
            { 200, true, true, 400.0f, true },
            { 100, false, true, 100.0f, true }
        };
        Town::Plan combinedPlan = Town::BuildPlan(combined);
        checks.push_back({ "vendor services precede nearest-first turn-ins",
            combinedPlan.steps == std::vector<Town::Step>{
                { Town::Service::Sell, 0 },
                { Town::Service::CreateRewardSpace, 0 },
                { Town::Service::Repair, 0 },
                { Town::Service::TurnInQuest, 100 },
                { Town::Service::TurnInQuest, 200 }
            } });

        Town::PlanningInput incidentalJunk;
        incidentalJunk.freeBagSlots = 10;
        incidentalJunk.hasSellableItems = true;
        incidentalJunk.hasInventoryVendor = true;
        incidentalJunk.completedQuests = { { 100, false, true, 25.0f, true } };
        checks.push_back({ "healthy bags do not detour for incidental junk",
            Town::BuildPlan(incidentalJunk).steps ==
                std::vector<Town::Step>{ { Town::Service::TurnInQuest, 100 } } });

        Town::PlanningInput missingVendor;
        missingVendor.freeBagSlots = 1;
        missingVendor.hasSellableItems = true;
        missingVendor.needsInventoryCleanup = true;
        missingVendor.completedQuests = {
            { 100, false, true, 25.0f, true },
            { 200, true, true, 50.0f, true }
        };
        Town::Plan missingVendorPlan = Town::BuildPlan(missingVendor);
        checks.push_back({ "missing vendor preserves safe turn-ins",
            missingVendorPlan.blockedByMissingVendor &&
            missingVendorPlan.steps == std::vector<Town::Step>{ { Town::Service::TurnInQuest, 100 } } });

        Town::PlanningInput protectedInventory;
        protectedInventory.freeBagSlots = 0;
        protectedInventory.hasInventoryVendor = true;
        protectedInventory.completedQuests = { { 200, true, true, 10.0f, true } };
        Town::Plan protectedPlan = Town::BuildPlan(protectedInventory);
        checks.push_back({ "protected inventory does not schedule a doomed turn-in",
            protectedPlan.blockedByProtectedInventory && protectedPlan.steps.size() == 1 &&
            protectedPlan.steps.front().service == Town::Service::CreateRewardSpace });

        Town::PlanningInput blockedLoot;
        blockedLoot.freeBagSlots = 0;
        blockedLoot.needsInventoryCleanup = true;
        blockedLoot.hasInventoryVendor = true;
        Town::Plan blockedLootPlan = Town::BuildPlan(blockedLoot);
        checks.push_back({ "blocked loot without sellable items gets a bounded cleanup attempt",
            blockedLootPlan.targetFreeBagSlots == 1 &&
            blockedLootPlan.steps == std::vector<Town::Step>{ { Town::Service::CreateRewardSpace, 0 } } });

        std::size_t passed = 0;
        std::ostringstream output;
        output << "[WorldBots Tests] deterministic logic scenarios\n";
        for (const Check& check : checks)
        {
            passed += check.passed ? 1 : 0;
            output << " - " << (check.passed ? "PASS" : "FAIL") << ": " << check.name << '\n';
        }
        output << "Result: " << passed << '/' << checks.size() << " passed";
        return output.str();
    }

    std::string ScenarioRunner::DescribeTownPlan(const std::string& botName, const Town::Plan& plan)
    {
        std::ostringstream output;
        output << "[WorldBots Town Plan] " << botName << '\n';
        output << " - Target free slots: " << plan.targetFreeBagSlots << '\n';
        output << " - Missing vendor: " << (plan.blockedByMissingVendor ? "Yes" : "No") << '\n';
        output << " - Protected inventory block: " << (plan.blockedByProtectedInventory ? "Yes" : "No") << '\n';
        if (plan.steps.empty())
        {
            output << " - Steps: none";
            return output.str();
        }

        output << " - Steps:";
        for (std::size_t index = 0; index < plan.steps.size(); ++index)
        {
            const Town::Step& step = plan.steps[index];
            output << "\n   " << (index + 1) << ". " << ServiceName(step.service);
            if (step.questId != 0)
                output << " (Quest " << step.questId << ')';
        }
        return output.str();
    }
}
