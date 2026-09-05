#include "Town/TownPlanning.h"
#include "Factory/BotRoster.h"
#include "Auth/BotLoginPolicy.h"
#include "Helper/ConsumableReservePolicy.h"
#include "Helper/ProgressionPolicy.h"
#include "Combat/CombatPositioning.h"
#include "Helper/RecoveryHubPolicy.h"
#include "Travel/TravelGraph.h"
#include "Diagnostics/DiagnosticsUtils.h"
#include "Helper/MathUtils.h"
#include "Helper/MovementPathPolicy.h"
#include "Helper/GrindFallbackPolicy.h"
#include "Travel/WorldTravelPolicy.h"
#include "Brain/ThreatAssessmentPolicy.h"
#include "Party/PartyCoordination.h"
#include "Party/PartyRecruitmentPolicy.h"
#include "Helper/VendorSelectionPolicy.h"

#include <algorithm>
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
        input.hasInventoryVendor = true;
        input.hasRepairVendor = true;
        input.hasRestockVendor = true;

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
        input.hasInventoryVendor = true;
        input.hasRepairVendor = true;
        input.hasRestockVendor = true;
        input.completedQuests = {
            { 200, true, true, 400.0f, true },
            { 100, false, true, 100.0f, true }
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
        input.hasInventoryVendor = true;
        input.completedQuests = { { 100, false, true, 25.0f, true } };

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
        input.hasInventoryVendor = false;
        input.completedQuests = {
            { 100, false, true, 25.0f, true },
            { 200, true, true, 50.0f, true }
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
        input.hasInventoryVendor = true;
        input.completedQuests = { { 200, true, true, 10.0f, true } };

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
        input.hasInventoryVendor = true;

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::CreateRewardSpace, 0 } },
            "full bags without standard sellable items should receive a bounded capacity cleanup attempt");
        Expect(plan.targetFreeBagSlots == 1,
            "blocked loot cleanup should require at least one observable free slot");
    }

    void TestReachableCrossMapTurnInsArePlanned()
    {
        Town::PlanningInput input;
        input.freeBagSlots = 10;
        input.completedQuests = {
            { 100, false, false, 1.0f, true },
            { 200, false, true, 2.0f, false },
            { 300, false, true, 3.0f, true }
        };

        Town::Plan plan = Town::BuildPlan(input);
        Expect(plan.steps == std::vector<Town::Step>{ { Town::Service::TurnInQuest, 300 } },
            "known turn-ins should be planned when the world graph can travel to their map");
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
        Expect(cautious.fleeHealthPct >= stress.fleeHealthPct,
            "cautious bots should flee at or above stress-test bots");
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

    void TestWorldTravelGraphModesAndRecovery()
    {
        using namespace Travel;

        TravelGraph graph;
        uint32_t town = graph.AddNode({ 0.0f, 0.0f, 0.0f, 0 },
            TravelNodeKind::FlightMaster, 10);
        uint32_t coast = graph.AddNode({ 1000.0f, 0.0f, 0.0f, 0 },
            TravelNodeKind::FlightMaster, 20);
        uint32_t harbour = graph.AddNode({ 1100.0f, 0.0f, 0.0f, 0 },
            TravelNodeKind::TransportStop, 500);
        uint32_t farHarbour = graph.AddNode({ 0.0f, 0.0f, 0.0f, 1 },
            TravelNodeKind::TransportStop, 500);
        graph.AddEdge(town, coast, TravelMode::FlightPath, 50.0f, 100);
        graph.AddEdge(coast, town, TravelMode::FlightPath, 50.0f, 101);
        graph.AddEdge(coast, harbour, TravelMode::Walk, 100.0f);
        graph.AddEdge(harbour, coast, TravelMode::Walk, 100.0f);
        uint64_t transportEdge = graph.AddEdge(harbour, farHarbour,
            TravelMode::Transport, 200.0f, 500);

        RouteOptions discovered;
        discovered.usableTaxiNodes = { 10, 20 };
        std::vector<RouteStep> route = graph.FindRoute(
            { 0.0f, 0.0f, 0.0f, 0 }, { 20.0f, 0.0f, 0.0f, 1 }, discovered);
        Expect(std::any_of(route.begin(), route.end(), [](const RouteStep& step) {
            return step.mode == TravelMode::FlightPath;
        }), "a discovered flight path should be used by the world route");
        Expect(std::any_of(route.begin(), route.end(), [](const RouteStep& step) {
            return step.mode == TravelMode::Transport;
        }), "a cross-map route should include its transport transition");

        RouteOptions undiscovered;
        std::vector<RouteStep> groundRoute = graph.FindRoute(
            { 0.0f, 0.0f, 0.0f, 0 }, { 20.0f, 0.0f, 0.0f, 1 }, undiscovered);
        Expect(std::none_of(groundRoute.begin(), groundRoute.end(), [](const RouteStep& step) {
            return step.mode == TravelMode::FlightPath;
        }), "undiscovered taxi nodes must not authorize a flight");

        discovered.blockedEdges.insert(transportEdge);
        std::vector<RouteStep> blocked = graph.FindRoute(
            { 0.0f, 0.0f, 0.0f, 0 }, { 20.0f, 0.0f, 0.0f, 1 }, discovered);
        Expect(blocked.empty(), "a failed transition should be excluded during route recovery");

        RouteOptions hearth;
        hearth.canUseHearthstone = true;
        hearth.home = { 10.0f, 0.0f, 0.0f, 1 };
        std::vector<RouteStep> hearthRoute = graph.FindRoute(
            { 500.0f, 500.0f, 0.0f, 0 }, { 20.0f, 0.0f, 0.0f, 1 }, hearth);
        Expect(!hearthRoute.empty() && hearthRoute.front().mode == TravelMode::Hearthstone,
            "the hearthstone should provide a valid directed fallback to the home map");

        // Test cross-zone overland direct walk route (e.g. 3,500 yards on the same map)
        RouteOptions sameMapOptions;
        std::vector<RouteStep> overlandRoute = graph.FindRoute(
            { 0.0f, 0.0f, 0.0f, 0 }, { 3500.0f, 0.0f, 0.0f, 0 }, sameMapOptions);
        Expect(!overlandRoute.empty() && overlandRoute.front().mode == TravelMode::Walk,
            "a 3,500 yard distance on the same map should create a direct walk route");

        // Beyond 6000 yards on the same map without graph nodes should not create a direct walk route
        TravelGraph emptyGraph;
        std::vector<RouteStep> tooFarRoute = emptyGraph.FindRoute(
            { 0.0f, 0.0f, 0.0f, 0 }, { 6500.0f, 0.0f, 0.0f, 0 }, sameMapOptions);
        Expect(tooFarRoute.empty(),
            "a 6,500 yard distance without connecting graph nodes should exceed direct walk limit");
    }

    void TestStarterAreaAndHomebindSafety()
    {
        using namespace Helper::RecoveryHubPolicy;

        // Starter area identification
        Expect(IsStarterArea(0, 18), "Northshire should be identified as starter area");
        Expect(IsStarterArea(0, 132), "Coldridge Valley should be identified as starter area");
        Expect(IsStarterArea(0, 154), "Deathknell should be identified as starter area");
        Expect(IsStarterArea(1, 363), "Valley of Trials should be identified as starter area");
        Expect(IsStarterArea(1, 141), "Shadowglen should be identified as starter area");
        Expect(IsStarterArea(1, 188), "Shadowglen subzone (188) should be identified as starter area");
        Expect(IsStarterArea(1, 220), "Camp Narache should be identified as starter area");
        Expect(IsStarterArea(530, 3526), "Sunstrider Isle should be identified as starter area");
        Expect(IsStarterArea(530, 3431), "Ammen Vale should be identified as starter area");

        // Non-starter areas
        Expect(!IsStarterArea(0, 12), "Elwynn Forest zone itself is not a starter sub-area");
        Expect(!IsStarterArea(0, 87), "Goldshire should not be a starter sub-area");
        Expect(!IsStarterArea(1, 14), "Durotar zone itself is not a starter sub-area");
        Expect(!IsStarterArea(1, 17), "The Barrens should not be a starter sub-area");

        // Starter zone identification
        Expect(IsStarterZone(0, 12), "Elwynn Forest should be identified as starter zone");
        Expect(IsStarterZone(0, 1), "Dun Morogh should be identified as starter zone");
        Expect(IsStarterZone(0, 85), "Tirisfal Glades should be identified as starter zone");
        Expect(IsStarterZone(1, 14), "Durotar should be identified as starter zone");
        Expect(IsStarterZone(1, 215), "Mulgore should be identified as starter zone");
        Expect(IsStarterZone(1, 141), "Teldrassil should be identified as starter zone");
        Expect(IsStarterZone(530, 3430), "Eversong Woods should be identified as starter zone");
        Expect(IsStarterZone(530, 3524), "Azuremyst Isle should be identified as starter zone");
        Expect(!IsStarterZone(1, 17), "The Barrens should not be a starter zone");
        Expect(!IsStarterZone(0, 130), "Silverpine Forest should not be a starter zone");
        Expect(!IsStarterZone(0, 40), "Westfall should not be a starter zone");

        // CanRecoverToHub tests
        Expect(!CanRecoverToHub(18, 1, 215, 215, true), "Level 18 bot in Mulgore must not recover to starter zone during progression");
        Expect(!CanRecoverToHub(14, 1, 14, 14, true), "Level 14 bot in Durotar must not recover to starter zone during progression");
        Expect(CanRecoverToHub(18, 1, 17, 17, true), "Level 18 bot in The Barrens can recover during progression");
        Expect(CanRecoverToHub(8, 1, 215, 215, true), "Level 8 bot in Mulgore can recover to starter zone");
        Expect(!CanRecoverToHub(8, 1, 215, 220, true), "Level 8 bot must not recover to starter sub-area");

        // CanUpdateHomebindTo: level 6+ bot upgrading from starter area
        Expect(CanUpdateHomebindTo(10, 0, 18, -8900.0f, -100.0f, 0, 87, -9400.0f, 0.0f),
            "level 10 bot should always upgrade homebind from starter area");
        // Level 10 bot should not bind to a starter area
        Expect(!CanUpdateHomebindTo(10, 0, 87, -9400.0f, 0.0f, 0, 18, -8900.0f, -100.0f),
            "level 10 bot must not bind to a starter area");
        // Level 18 bot upgrading from Mulgore starter zone to The Barrens
        Expect(CanUpdateHomebindTo(18, 1, 215, -2360.0f, -390.0f, 1, 17, -457.0f, -2639.0f, 17, 215),
            "level 18 bot should upgrade homebind from Mulgore to The Barrens");
        // Level 18 bot must not bind to a starter zone
        Expect(!CanUpdateHomebindTo(18, 1, 17, -457.0f, -2639.0f, 1, 215, -2360.0f, -390.0f, 215, 17),
            "level 18 bot must not bind to a starter zone");
    }

    void TestDiagnosticsUtils()
    {
        std::string cleaned = Diagnostics::CleanField("line1\tcol2\r\ncol3\n");
        Expect(cleaned == "line1 col2  col3 ", "CleanField should replace tabs and newlines with spaces");

        std::ostringstream streamOut;
        Diagnostics::WriteCleanField(streamOut, "stream\tcol2\r\ncol3\n");
        Expect(streamOut.str() == "stream col2  col3 ", "WriteCleanField should stream clean text without allocations");

        std::ostringstream adapterOut;
        adapterOut << Diagnostics::AsCleanField("adapter\ttest\n");
        Expect(adapterOut.str() == "adapter test ", "AsCleanField streaming adapter should clean fields properly");

        Expect(Diagnostics::NormalizeName("   Botharry   ") == "botharry", "NormalizeName should trim and lowercase");
        Expect(Diagnostics::NormalizeName("Botharry The Great") == "botharry the great", "NormalizeName should preserve internal spaces");
        Expect(Diagnostics::NormalizeName("  \t\n  ").empty(), "NormalizeName on whitespace-only should be empty");

        std::string ts = Diagnostics::FormatTimestampIso(1700000000000ULL);
        Expect(!ts.empty() && ts.find("2023") != std::string::npos, "FormatTimestampIso should produce valid date");

        Expect(Diagnostics::EnsureParentDirectory("build_test_dir/nested/dummy.tsv"), "EnsureParentDirectory should succeed");
        std::error_code ec;
        std::filesystem::remove_all("build_test_dir", ec);
    }

    void TestNavigationMathAndHash()
    {
        float d3 = Helper::Distance3D(0.0f, 0.0f, 0.0f, 3.0f, 4.0f, 12.0f);
        Expect(std::abs(d3 - 13.0f) < 0.001f, "Distance3D(3, 4, 12) should equal 13");

        Helper::MovementPathPolicy::Point p1{ 10.0f, 20.0f, 30.0f };
        Helper::MovementPathPolicy::Point p2{ 13.0f, 24.0f, 42.0f };
        float policyD3 = Helper::MovementPathPolicy::Distance3D(p1, p2);
        Expect(std::abs(policyD3 - 13.0f) < 0.001f, "MovementPathPolicy::Distance3D should match 13");

        uint64_t hash1 = Helper::HashUtils::FNV1aBasis;
        Helper::HashUtils::MixHash(hash1, static_cast<uint32_t>(100));
        Helper::HashUtils::MixHash(hash1, static_cast<uint32_t>(200));

        uint64_t hash2 = Helper::HashUtils::FNV1aBasis;
        Helper::HashUtils::MixHash(hash2, static_cast<uint32_t>(200));
        Helper::HashUtils::MixHash(hash2, static_cast<uint32_t>(100));

        Expect(hash1 != 0, "FNV-1a hash must not be 0");
        Expect(hash1 != hash2, "FNV-1a hash should be sensitive to argument order");
    }

    void TestGrindAndTravelPolicies()
    {
        // Grind fallback policy tests
        Expect(!Helper::GrindFallbackPolicy::ShouldEndAfterUnreachableAnchor(2),
            "2 unreachable anchors should not end grind");
        Expect(Helper::GrindFallbackPolicy::ShouldEndAfterUnreachableAnchor(3),
            "3 unreachable anchors should end grind");
        Expect(!Helper::GrindFallbackPolicy::ShouldEndAfterAbsentAnchorMisses(9),
            "9 absent anchor misses should not end grind");
        Expect(Helper::GrindFallbackPolicy::ShouldEndAfterAbsentAnchorMisses(10),
            "10 absent anchor misses should end grind");
        Expect(Helper::GrindFallbackPolicy::AbsentAnchorSuppressionSeconds == 90,
            "AbsentAnchorSuppressionSeconds should be 90s");
        Expect(Helper::GrindFallbackPolicy::UnreachableAnchorSuppressionSeconds == 900,
            "UnreachableAnchorSuppressionSeconds should be 900s");

        // Travel policy tests
        Expect(!Travel::WorldTravelPolicy::HasHearthstoneTimedOut(19999),
            "Hearthstone under 20s should not time out");
        Expect(Travel::WorldTravelPolicy::HasHearthstoneTimedOut(20000),
            "Hearthstone at 20s should time out");
        Expect(Travel::WorldTravelPolicy::CanPlanHearthstone(true, false),
            "Usable hearthstone not failed earlier should plan");
        Expect(!Travel::WorldTravelPolicy::CanPlanHearthstone(true, true),
            "Hearthstone failed earlier in journey should not plan");
        Expect(!Travel::WorldTravelPolicy::CanPlanHearthstone(false, false),
            "Unusable hearthstone should not plan");
    }

    void TestDynamicFleeThreatPolicy()
    {
        using namespace Brain;

        // Verify armor tiers for classes
        Expect(GetArmorTierForClass(8) == CombatArmorTier::Cloth, "Mage should be Cloth");
        Expect(GetArmorTierForClass(5) == CombatArmorTier::Cloth, "Priest should be Cloth");
        Expect(GetArmorTierForClass(9) == CombatArmorTier::Cloth, "Warlock should be Cloth");
        Expect(GetArmorTierForClass(4) == CombatArmorTier::Leather, "Rogue should be Leather");
        Expect(GetArmorTierForClass(3) == CombatArmorTier::Leather, "Hunter should be Leather");
        Expect(GetArmorTierForClass(11) == CombatArmorTier::Leather, "Druid should be Leather");
        Expect(GetArmorTierForClass(1) == CombatArmorTier::MailOrPlate, "Warrior should be MailOrPlate");
        Expect(GetArmorTierForClass(2) == CombatArmorTier::MailOrPlate, "Paladin should be MailOrPlate");
        Expect(GetArmorTierForClass(7) == CombatArmorTier::MailOrPlate, "Shaman should be MailOrPlate");

        // Verify thresholds by pack size and armor tier
        // 1 attacker: standard 20%
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::Cloth, 1, 0) == 20, "1 attacker threshold should be 20");
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::MailOrPlate, 1, 0) == 20, "1 attacker threshold should be 20");

        // 2 equal-level attackers: Cloth 50%, Leather 40%, Plate 35%
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::Cloth, 2, 0) == 50, "Cloth 2 attackers equal level should be 50%");
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::Leather, 2, 0) == 40, "Leather 2 attackers equal level should be 40%");
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::MailOrPlate, 2, 0) == 35, "Plate 2 attackers equal level should be 35%");

        // 2 attackers +2 levels: Cloth 65%, Leather 55%, Plate 45%
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::Cloth, 2, 2) == 65, "Cloth 2 attackers +2 level should be 65%");
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::MailOrPlate, 2, 2) == 45, "Plate 2 attackers +2 level should be 45%");

        // 3 equal-level attackers: Cloth 65%, Leather 55%, Plate 45%
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::Cloth, 3, 0) == 65, "Cloth 3 attackers equal level should be 65%");
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::MailOrPlate, 3, 0) == 45, "Plate 3 attackers equal level should be 45%");

        // 4+ attackers: 85% immediate escape
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::Cloth, 4, 0) == 85, "4 attackers should trigger 85% escape");
        Expect(CalculateMultiAttackerFleeThreshold(CombatArmorTier::MailOrPlate, 5, 0) == 85, "5 attackers should trigger 85% escape");

        // Test ShouldFleeFromMultipleAttackers behavior
        MultiAttackerRiskInput clothInput;
        clothInput.botLevel = 20;
        clothInput.highestAttackerLevel = 20;
        clothInput.attackerCount = 2;
        clothInput.healthPct = 45;
        clothInput.playerClass = 8; // Mage
        Expect(ShouldFleeFromMultipleAttackers(clothInput), "Mage at 45% HP vs 2 equal mobs must flee (threshold 50%)");

        MultiAttackerRiskInput warriorInput;
        warriorInput.botLevel = 20;
        warriorInput.highestAttackerLevel = 20;
        warriorInput.attackerCount = 2;
        warriorInput.healthPct = 45;
        warriorInput.playerClass = 1; // Warrior
        Expect(!ShouldFleeFromMultipleAttackers(warriorInput), "Warrior at 45% HP vs 2 equal mobs should keep fighting (threshold 35%)");

        warriorInput.healthPct = 30;
        Expect(ShouldFleeFromMultipleAttackers(warriorInput), "Warrior at 30% HP vs 2 equal mobs must flee (threshold 35%)");

        // Test AboveLevelAttacker flee behavior
        AboveLevelAttackerRiskInput aboveInput;
        aboveInput.botLevel = 20;
        aboveInput.attackerLevel = 22; // +2 level
        aboveInput.engaged = true;
        aboveInput.healthPct = 45;
        aboveInput.playerClass = 8; // Mage (threshold 50%)
        Expect(ShouldFleeFromAboveLevelAttacker(aboveInput), "Mage at 45% HP vs +2 mob must flee (threshold 50%)");

        aboveInput.playerClass = 1; // Warrior (threshold 30%)
        Expect(!ShouldFleeFromAboveLevelAttacker(aboveInput), "Warrior at 45% HP vs +2 mob should keep fighting (threshold 30%)");
    }

    void TestBreadcrumbBacktrackingAndConfinedEscape()
    {
        using namespace Helper::MovementPathPolicy;

        std::vector<Point> breadcrumbs = {
            { 100.0f, 100.0f, 10.0f }, // oldest (entrance)
            { 110.0f, 100.0f, 10.0f },
            { 120.0f, 100.0f, 10.0f },
            { 130.0f, 100.0f, 10.0f }, // newest (deep in cave)
        };

        Point botPos{ 132.0f, 100.0f, 10.0f };
        std::vector<Point> candidates = CollectBacktrackBreadcrumbs(breadcrumbs, botPos, 4.0f, 25.0f);

        // Should find {120, 100, 10} (dist 12) and {110, 100, 10} (dist 22) in reverse order
        Expect(candidates.size() == 2, "Should collect 2 breadcrumbs in [4, 25] range");
        if (candidates.size() >= 2)
        {
            Expect(candidates[0].x == 120.0f, "First candidate should be most recent valid breadcrumb");
            Expect(candidates[1].x == 110.0f, "Second candidate should be older breadcrumb");
        }

        // Test Confined Radial Escape Acceptance
        Expect(ShouldAcceptConfinedRadialEscape(6.0f), "Separation gain of 6.0 yds down tunnel should be accepted in confined space");
        Expect(!ShouldAcceptConfinedRadialEscape(3.0f), "Separation gain of 3.0 yds should be rejected in confined space");
    }

    void TestProgressionQuestGroupPolicy()
    {
        // Solo bots reject dungeon and raid quests
        Expect(!Helper::IsQuestGroupStructureSuitable(false, 81, 1), "Solo bot should reject dungeon quest (81)");
        Expect(!Helper::IsQuestGroupStructureSuitable(false, 62, 1), "Solo bot should reject raid quest (62)");
        Expect(!Helper::IsQuestGroupStructureSuitable(false, 88, 1), "Solo bot should reject raid 10 quest (88)");
        Expect(!Helper::IsQuestGroupStructureSuitable(false, 89, 1), "Solo bot should reject raid 25 quest (89)");
        Expect(!Helper::IsQuestGroupStructureSuitable(false, 0, 3), "Solo bot should reject quest requiring 3 players");
        Expect(!Helper::IsQuestGroupStructureSuitable(false, 0, 5), "Solo bot should reject quest requiring 5 players");

        // Solo bots accept normal solo quests
        Expect(Helper::IsQuestGroupStructureSuitable(false, 0, 0), "Solo bot should accept normal quest with 0 suggested players");
        Expect(Helper::IsQuestGroupStructureSuitable(false, 0, 1), "Solo bot should accept normal quest with 1 suggested player");

        // Grouped bots accept all group quests
        Expect(Helper::IsQuestGroupStructureSuitable(true, 81, 5), "Grouped bot should accept dungeon quest");
        Expect(Helper::IsQuestGroupStructureSuitable(true, 62, 40), "Grouped bot should accept raid quest");
        Expect(Helper::IsQuestGroupStructureSuitable(true, 0, 3), "Grouped bot should accept multi-player quest");
    }

    void TestPartyCoordinationAndRoleAssignment()
    {
        // 1. Assign roles with declared roles
        // Member 1: Warrior (class 1), no declared role
        // Member 2: Paladin (class 2), declared tank (0x02)
        // Member 3: Priest (class 5), declared healer (0x04)
        std::vector<Party::MemberDescriptor> members = {
            { 101, 1, 0, false },
            { 102, 2, 0x02, false },
            { 103, 5, 0x04, false },
        };

        Party::RoleAssignment assignment = Party::AssignRoles(members);
        Expect(assignment.tankId == 102, "Declared tank should be selected as tank");
        Expect(assignment.healerId == 103, "Declared healer should be selected as healer");
        Expect(assignment.GetRole(101) == Party::Role::Damage, "Undeclared member should be damage");
        Expect(assignment.GetRole(102) == Party::Role::Tank, "Member 102 should be tank role");
        Expect(assignment.GetRole(103) == Party::Role::Healer, "Member 103 should be healer role");

        // 2. Declared healer on hybrid class (Paladin) disqualifies it from tanking
        // Member 1: Warrior (class 1), no declared role
        // Member 2: Paladin (class 2), declared healer (0x04)
        std::vector<Party::MemberDescriptor> hybridMembers = {
            { 201, 1, 0, false },
            { 202, 2, 0x04, false },
        };
        Party::RoleAssignment hybridAssignment = Party::AssignRoles(hybridMembers);
        Expect(hybridAssignment.tankId == 201, "Warrior should be tank when Paladin declared healer");
        Expect(hybridAssignment.healerId == 202, "Paladin declared healer should be healer");

        // 3. Formations
        Party::FormationSlot tankSlot = Party::ChooseFormation(Party::Role::Tank, 0);
        Expect(tankSlot.distance == 2.5f && tankSlot.angle == 0.0f, "Tank should lead formation");

        Party::FormationSlot healerSlot = Party::ChooseFormation(Party::Role::Healer, 1);
        Expect(healerSlot.distance == 7.0f, "Healer should trail formation");

        // 4. Shared quests
        std::vector<uint32_t> localQuests = { 10, 20, 30 };
        std::vector<uint32_t> leaderQuests = { 20, 40 };
        Expect(Party::SelectSharedQuest(localQuests, leaderQuests) == 20, "Shared quest 20 should be selected");
        Expect(Party::SelectSharedQuest(localQuests, leaderQuests, 20) == 20, "Preferred shared quest should match");
    }

    void TestVendorSelectionVerticalWeighting()
    {
        // Candidate A: 20 yards horizontally, but 50 yards vertically (cliff / elevator)
        float scoreA = Helper::VendorSelectionPolicy::CalculateWeightedDistanceSq(20.0f, 0.0f, 50.0f);
        // Candidate B: 40 yards horizontally, but 2 yards vertically (same walkable level)
        float scoreB = Helper::VendorSelectionPolicy::CalculateWeightedDistanceSq(40.0f, 0.0f, 2.0f);

        // Candidate B should have a significantly lower (better) score than Candidate A
        Expect(scoreB < scoreA, "Walkable same-level vendor at 40yd should be preferred over 50yd vertical cliff vendor at 20yd horizontal");

        // Small vertical delta (<= 8.0yd, e.g. stairs/ramps) should not have vertical multiplier
        float flatScore = Helper::VendorSelectionPolicy::CalculateWeightedDistanceSq(10.0f, 0.0f, 5.0f);
        // dx^2 + dy^2 + dz^2 = 100 + 0 + 25 = 125
        Expect(std::abs(flatScore - 125.0f) < 0.01f, "Small vertical delta (5yd) should not incur penalty multiplier");

        // Large vertical delta (> 8.0yd) should apply 3x multiplier to dz
        float steepScore = Helper::VendorSelectionPolicy::CalculateWeightedDistanceSq(10.0f, 0.0f, 10.0f);
        // dx^2 + dy^2 + (10 * 3)^2 = 100 + 0 + 900 = 1000
        Expect(std::abs(steepScore - 1000.0f) < 0.01f, "Large vertical delta (10yd) should apply 3x penalty multiplier");
    }

    void TestProgressionEcologyDistanceThresholds()
    {
        // Within 180yd (CompletePathPreflightRadius), complete ground path is required
        Expect(Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(50.0f) == true, "50yd should require complete preflight");
        Expect(Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(150.0f) == true, "150yd should require complete preflight");
        Expect(Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(180.0f) == true, "180yd should require complete preflight");

        // Beyond 180yd (e.g. Crossroads to wild mobs 250yd-1800yd away), complete preflight must NOT be required
        Expect(Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(185.0f) == false, "185yd should not require complete preflight");
        Expect(Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(300.0f) == false, "300yd should not require complete preflight");
        Expect(Helper::GrindFallbackPolicy::ShouldRequireCompletePathPreflight(1200.0f) == false, "1200yd should not require complete preflight");
    }

    void TestPartyCandidateScoring()
    {
        Party::CandidateDescriptor t1;
        t1.guid = 1;
        t1.level = 10;
        t1.classId = 1; // Warrior (Tank)
        t1.distance = 50.0f;
        t1.hasQuestInLog = true;
        t1.canAcceptQuest = false;
        t1.isSolo = true;
        t1.currentGroupSize = 1;

        Party::CandidateDescriptor t2;
        t2.guid = 2;
        t2.level = 10;
        t2.classId = 1;
        t2.distance = 50.0f;
        t2.hasQuestInLog = false;
        t2.canAcceptQuest = true;
        t2.isSolo = true;
        t2.currentGroupSize = 1;

        int32_t scoreT1 = Party::ScoreCandidate(t1, 10, 4); // Recruiter Rogue (DPS)
        int32_t scoreT2 = Party::ScoreCandidate(t2, 10, 4);

        Expect(scoreT1 > scoreT2, "Tier 1 candidate (in log) must score higher than Tier 2 (proximity eligible)");
        Expect(scoreT1 - scoreT2 == 500, "Base difference between Tier 1 and Tier 2 candidate should be 500 points");

        // Distance penalty test
        Party::CandidateDescriptor farCand = t1;
        farCand.distance = 250.0f;
        int32_t scoreFar = Party::ScoreCandidate(farCand, 10, 4);
        Expect(scoreFar < scoreT1, "Farther candidate should receive a distance penalty");

        // Level difference penalty test
        Party::CandidateDescriptor diffLevelCand = t1;
        diffLevelCand.level = 12; // 2 levels higher -> -60 points
        int32_t scoreDiffLevel = Party::ScoreCandidate(diffLevelCand, 10, 4);
        Expect(scoreDiffLevel == scoreT1 - 60, "2 levels difference should incur 60 point penalty");

        // Disqualification test
        Party::CandidateDescriptor disqualified;
        disqualified.hasQuestInLog = false;
        disqualified.canAcceptQuest = false;
        Expect(Party::ScoreCandidate(disqualified, 10, 4) < 0, "Candidate who cannot accept and does not have quest should be disqualified");

        Party::CandidateDescriptor fullGroup;
        fullGroup.hasQuestInLog = true;
        fullGroup.isSolo = false;
        fullGroup.currentGroupSize = 5;
        Expect(Party::ScoreCandidate(fullGroup, 10, 4) < 0, "Candidate already in a full group should be disqualified");
    }

    void TestCandidateRankingTierPriority()
    {
        Party::CandidateDescriptor c1; // Tier 2, close (50yd)
        c1.guid = 1;
        c1.level = 10;
        c1.distance = 50.0f;
        c1.hasQuestInLog = false;
        c1.canAcceptQuest = true;

        Party::CandidateDescriptor c2; // Tier 1, far (300yd)
        c2.guid = 2;
        c2.level = 10;
        c2.distance = 300.0f;
        c2.hasQuestInLog = true;
        c2.canAcceptQuest = false;

        Party::CandidateDescriptor c3; // Tier 1, close (50yd)
        c3.guid = 3;
        c3.level = 10;
        c3.distance = 50.0f;
        c3.hasQuestInLog = true;
        c3.canAcceptQuest = false;

        Party::CandidateDescriptor c4; // Disqualified
        c4.guid = 4;
        c4.level = 10;
        c4.distance = 10.0f;
        c4.hasQuestInLog = false;
        c4.canAcceptQuest = false;

        std::vector<Party::CandidateDescriptor> candidates = { c1, c2, c3, c4 };
        auto ranked = Party::RankCandidates(candidates, 10, 0);

        Expect(ranked.size() == 3, "Ranked candidates should exclude disqualified bots");
        Expect(ranked[0].guid == 3, "Tier 1 close candidate should rank 1st");
        Expect(ranked[1].guid == 2, "Tier 1 far candidate should rank 2nd");
        Expect(ranked[2].guid == 1, "Tier 2 candidate should rank 3rd");
    }

    void TestGroupContinuity()
    {
        Expect(!Party::ShouldKeepGroupTogether(false, false, 2),
            "Group with no shared active quest and no chain follow-up should not stay together");
        Expect(Party::ShouldKeepGroupTogether(true, false, 2),
            "Group with shared active quest should stay together");
        Expect(Party::ShouldKeepGroupTogether(false, true, 2),
            "Group with shared chain follow-up should stay together");
        Expect(!Party::ShouldKeepGroupTogether(true, true, 1),
            "Solo bot (size 1) should not maintain group");
    }
}

int main()
{
    TestProgressionQuestGroupPolicy();
    TestPartyCoordinationAndRoleAssignment();
    TestDiagnosticsUtils();
    TestNavigationMathAndHash();
    TestGrindAndTravelPolicies();
    TestNoNeedsProducesNoPlan();
    TestCombinedVisitOrdersServicesBeforeTurnIn();
    TestTurnInDoesNotDetourForIncidentalJunk();
    TestMissingVendorBlocksVendorWorkButNotSafeTurnIn();
    TestProtectedInventoryDoesNotPromiseRewardSpace();
    TestBlockedLootWithoutSellableItemsGetsCleanupAttempt();
    TestReachableCrossMapTurnInsArePlanned();
    TestRosterParsingAndCycling();
    TestBehaviorProfileTuning();
    TestDedicatedAccountNaming();
    TestLoginRetryBackoff();
    TestRecoveryStackSelection();
    TestProgressionDifficultyPolicy();
    TestCombatRangePolicy();
    TestWorldTravelGraphModesAndRecovery();
    TestStarterAreaAndHomebindSafety();
    TestDynamicFleeThreatPolicy();
    TestBreadcrumbBacktrackingAndConfinedEscape();
    TestVendorSelectionVerticalWeighting();
    TestProgressionEcologyDistanceThresholds();
    TestPartyCandidateScoring();
    TestCandidateRankingTierPriority();
    TestGroupContinuity();

    if (failures != 0)
    {
        std::cerr << failures << " WorldBots logic test(s) failed.\n";
        return 1;
    }

    std::cout << "All WorldBots logic tests passed.\n";
    return 0;
}
