#include "Town/TownPlanning.h"
#include "Factory/BotRoster.h"
#include "Auth/BotLoginPolicy.h"
#include "Helper/ConsumableReservePolicy.h"
#include "Helper/ProgressionPolicy.h"
#include "Combat/CombatPositioning.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void Expect(bool condition, const std::string& message)
    {
        if (condition)
            return;

        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }

    void TestNoNeedsProducesNoPlan()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 10;
        input.hasUsableVendor = true;

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.Empty(), "a bot with no town needs should not start a town run");
        Expect(!plan.blockedByMissingVendor, "an unused vendor should not affect an empty plan");
    }

    void TestCombinedVisitOrdersServicesBeforeTurnIn()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 0;
        input.hasSellableItems = true;
        input.needsInventoryCleanup = true;
        input.needsRepair = true;
        input.hasUsableVendor = true;
        input.completedQuests = {
            { 200, true, true, true, 400.0f },
            { 100, false, true, true, 100.0f }
        };

        Town::Plan plan = Town::BuildPlan(input);
        std::vector<Town::Step> expected = {
            { Town::Service::Sell, 0 },
            { Town::Service::CreateRewardSpace, 0 },
            { Town::Service::Repair, 0 },
            { Town::Service::TurnInQuest, 100 },
            { Town::Service::TurnInQuest, 200 }
        };
        Expect(plan.steps == expected, "vendor services should precede nearest-first quest turn-ins");
        Expect(plan.RequiresVendorVisit(), "combined plan should expose its vendor dependency");
    }

    void TestTurnInDoesNotDetourForIncidentalJunk()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 10;
        input.hasSellableItems = true;
        input.hasUsableVendor = true;
        input.completedQuests = { { 100, false, true, true, 25.0f } };

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::TurnInQuest, 100 } },
            "incidental junk should not force a vendor detour when bag space is healthy");
    }

    void TestMissingVendorBlocksVendorWorkButNotSafeTurnIn()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 1;
        input.hasSellableItems = true;
        input.needsInventoryCleanup = true;
        input.hasUsableVendor = false;
        input.completedQuests = {
            { 100, false, true, true, 25.0f },
            { 200, true, true, true, 50.0f }
        };

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.blockedByMissingVendor, "missing vendor should be reported explicitly");
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::TurnInQuest, 100 } },
            "safe turn-ins should remain actionable while blocked rewards wait");
    }

    void TestProtectedInventoryDoesNotPromiseRewardSpace()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 0;
        input.hasSellableItems = false;
        input.hasUsableVendor = true;
        input.completedQuests = { { 200, true, true, true, 10.0f } };

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.blockedByProtectedInventory, "protected-only inventory should be reported as blocked");
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::CreateRewardSpace, 0 } },
            "the planner should attempt cleanup but must not schedule a doomed turn-in");
    }

    void TestBlockedLootWithoutSellableItemsGetsCleanupAttempt()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 0;
        input.needsInventoryCleanup = true;
        input.hasSellableItems = false;
        input.hasUsableVendor = true;

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::CreateRewardSpace, 0 } },
            "full bags without standard sellable items should receive a bounded capacity cleanup attempt");
        Expect(plan.targetFreeBagSlots == 1,
            "blocked loot cleanup should require at least one observable free slot");
    }

    void TestCrossMapAndUnknownTurnInsAreIgnored()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 10;
        input.completedQuests = {
            { 100, false, false, true, 1.0f },
            { 200, false, true, false, 2.0f },
            { 300, false, true, true, 3.0f }
        };

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::TurnInQuest, 300 } },
            "only reachable, positioned turn-ins should be planned");
    }

    void TestServiceVerificationRequiresObservableProgress()
    {
        using Town::VerificationResult;
        Expect(Town::VerifyInventoryService(Town::Service::Sell, 0, 2, 100, 100, false) == VerificationResult::Succeeded,
            "selling that frees capacity should succeed");
        Expect(Town::VerifyInventoryService(Town::Service::Sell, 0, 0, 100, 100, false) == VerificationResult::RetryableFailure,
            "selling without freeing capacity should not report success");
        Expect(Town::VerifyInventoryService(Town::Service::Repair, 2, 2, 30, 100, false) == VerificationResult::Succeeded,
            "a completed durability repair should succeed");
        Expect(Town::VerifyInventoryService(Town::Service::Repair, 2, 2, 30, 30, true) == VerificationResult::Blocked,
            "a repair with no durability change should be blocked");
    }

    void TestRosterParsingAndCycling()
    {
        std::vector<std::string> errors;
        std::vector<Factory::BotDefinition> roster = Factory::ParseBotRoster(
            "1:1:0:cautious, 1:8:1:questing, 9:99:0:balanced", &errors);
        Expect(roster.size() == 2, "valid mixed-roster entries should be retained");
        Expect(errors.size() == 1, "invalid roster entries should be reported and ignored");
        Expect(roster[0] == Factory::BotDefinition{ 1, 1, 0, Factory::BehaviorProfile::Cautious },
            "the first roster entry should preserve its character and behavior profile");
        Expect(Factory::SelectBotDefinition(roster, 2, {}) == roster[0],
            "a short roster pattern should repeat deterministically for scaled bot counts");
    }

    void TestBehaviorProfileTuning()
    {
        Factory::BehaviorTuning cautious = Factory::GetBehaviorTuning(Factory::BehaviorProfile::Cautious);
        Factory::BehaviorTuning stress = Factory::GetBehaviorTuning(Factory::BehaviorProfile::Stress);
        Expect(cautious.fleeHealthPct > stress.fleeHealthPct,
            "cautious bots should flee earlier than stress-test bots");
        Expect(cautious.restHealthPct > stress.restHealthPct,
            "cautious bots should recover sooner than stress-test bots");
    }

    void TestDedicatedAccountNaming()
    {
        std::string first = Factory::GenerateDedicatedAccountName("w-bot prefix", 0);
        std::string later = Factory::GenerateDedicatedAccountName("w-bot prefix", 1000000);
        Expect(first == "WBOTPREFI0000001", "account prefixes should be sanitized and names should be deterministic");
        Expect(first.size() <= 16 && later.size() <= 16,
            "dedicated account names must fit TrinityCore's account-name limit");
        Expect(first != later, "different bot slots must receive different account names");
        Expect(Factory::HasDedicatedAccountPrefix(first, "w-bot prefix"),
            "managed account detection should use the normalized reserved prefix");
    }

    void TestLoginRetryBackoff()
    {
        Expect(BotAuth::CalculateRetryDelayMs(0, 5000, 60000) == 5000,
            "the first login retry should use the configured initial delay");
        Expect(BotAuth::CalculateRetryDelayMs(1, 5000, 60000) == 10000,
            "login retry delays should double after each failed attempt");
        Expect(BotAuth::CalculateRetryDelayMs(8, 5000, 60000) == 60000,
            "login retry backoff should stop at the configured maximum");
        Expect(BotAuth::CalculateRetryDelayMs(1000, 5000, 60000) == 60000,
            "large attempt counts should remain bounded without overflowing");
    }

    void TestRecoveryStackSelection()
    {
        std::vector<Helper::RecoveryStackCandidate> candidates = {
            { 10, 5, 20, true, false, true },
            { 11, 10, 7, true, false, true },
            { 12, 5, 20, false, true, true },
            { 13, 12, 3, false, true, false }
        };

        Helper::RecoveryReserveSelection manaUser =
            Helper::SelectRecoveryReserves(candidates, true);
        Expect(manaUser.foodPosition == 11,
            "the best usable food stack should be retained");
        Expect(manaUser.drinkPosition == 12,
            "mana users should retain the best usable drink stack");

        Helper::RecoveryReserveSelection nonManaUser =
            Helper::SelectRecoveryReserves(candidates, false);
        Expect(nonManaUser.foodPosition == 11,
            "non-mana users should still retain one food stack");
        Expect(nonManaUser.drinkPosition == Helper::RecoveryReserveSelection::NoPosition,
            "non-mana users should not reserve a drink stack");
    }

    void TestProgressionDifficultyPolicy()
    {
        Expect(Helper::IsQuestLevelSuitable(10, 11, 1),
            "a quest one level above the bot should remain eligible");
        Expect(!Helper::IsQuestLevelSuitable(10, 12, 1),
            "a quest above the configured solo ceiling should be deferred");
        Expect(Helper::IsQuestLevelSuitable(10, -1, 1),
            "player-scaled quests should remain eligible");

        Expect(Helper::IsGrindingLevelSuitable(10, 7, -3, 0),
            "the lower edge of the conservative grind band should be eligible");
        Expect(Helper::IsGrindingLevelSuitable(10, 10, -3, 0),
            "an equal-level normal enemy should be eligible");
        Expect(!Helper::IsGrindingLevelSuitable(10, 11, -3, 0),
            "above-level enemies should be rejected by the default grind band");
        Expect(Helper::NextProgressionRetryLevel(10) == 11,
            "a failed progression attempt should wait for one gained level");
    }

    void TestCombatRangePolicy()
    {
        using Combat::CombatPositioning;
        using Combat::RangeAdjustment;

        Expect(CombatPositioning::ChooseRangeAdjustment(30.0f, 8.0f, 28.0f, true) ==
            RangeAdjustment::CloseDistance, "ranged combat should close when outside maximum range");
        Expect(CombatPositioning::ChooseRangeAdjustment(20.0f, 8.0f, 28.0f, false) ==
            RangeAdjustment::CloseDistance, "ranged combat should close until line of sight is restored");
        Expect(CombatPositioning::ChooseRangeAdjustment(5.0f, 8.0f, 28.0f, true) ==
            RangeAdjustment::CreateDistance, "ranged combat should retreat inside its dead zone");
        Expect(CombatPositioning::ChooseRangeAdjustment(20.0f, 8.0f, 28.0f, true) ==
            RangeAdjustment::Hold, "ranged combat should hold a valid firing position");
    }
}

int main()
{
    TestNoNeedsProducesNoPlan();
    TestCombinedVisitOrdersServicesBeforeTurnIn();
    TestTurnInDoesNotDetourForIncidentalJunk();
    TestMissingVendorBlocksVendorWorkButNotSafeTurnIn();
    TestProtectedInventoryDoesNotPromiseRewardSpace();
    TestBlockedLootWithoutSellableItemsGetsCleanupAttempt();
    TestCrossMapAndUnknownTurnInsAreIgnored();
    TestServiceVerificationRequiresObservableProgress();
    TestRosterParsingAndCycling();
    TestBehaviorProfileTuning();
    TestDedicatedAccountNaming();
    TestLoginRetryBackoff();
    TestRecoveryStackSelection();
    TestProgressionDifficultyPolicy();
    TestCombatRangePolicy();

    if (failures != 0)
    {
        std::cerr << failures << " WorldBots logic test(s) failed.\n";
        return 1;
    }

    std::cout << "All WorldBots logic tests passed.\n";
    return 0;
}
