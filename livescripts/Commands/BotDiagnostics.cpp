#include "BotDiagnostics.h"
#include "Actions/LootAction.h"
#include "Brain/BotBrain.h"
#include "Brain/GoalTier.h"
#include "Config/BotConfig.h"
#include "Diagnostics/BotTrace.h"
#include "Factory/BotFactory.h"
#include "Helper/InventoryUtils.h"
#include "Helper/Constants.h"
#include "Helper/MathUtils.h"
#include "Helper/MovementManager.h"
#include "Helper/NpcFinder.h"
#include "Helper/QuestUtils.h"
#include "Creature.h"
#include "Globals/ObjectMgr.h"
#include "Item.h"
#include "Player.h"
#include "Log.h"
#include <sstream>
#include <string>

namespace Commands
{
    static std::string EmitBotStatus(std::string status)
    {
        TC_LOG_INFO("server", "[WorldBots] [Status]\n{}", status);
        return status;
    }
    
        static const char* QuestObjectiveTypeName(Blackboard::QuestObjectiveType type)
    {
        switch (type)
        {
            case Blackboard::QuestObjectiveType::KillCreature: return "KillCreature";
            case Blackboard::QuestObjectiveType::CollectItem: return "CollectItem";
            case Blackboard::QuestObjectiveType::TalkToCreature: return "Creature";
            case Blackboard::QuestObjectiveType::CastOnCreature: return "CastOnCreature";
            case Blackboard::QuestObjectiveType::InteractGameObject: return "InteractGameObject";
            case Blackboard::QuestObjectiveType::Explore: return "Explore";
            case Blackboard::QuestObjectiveType::Unsupported: return "Unsupported";
            default: return "Unknown";
        }
    }
    
        static const char* InventoryResultName(InventoryResult result)
    {
        switch (result)
        {
            case EQUIP_ERR_OK: return "OK";
            case EQUIP_ERR_INVENTORY_FULL: return "INVENTORY_FULL";
            case EQUIP_ERR_CANT_CARRY_MORE_OF_THIS: return "CANT_CARRY_MORE";
            case EQUIP_ERR_ITEM_CANT_STACK: return "ITEM_CANT_STACK";
            default: return "OTHER";
        }
    }

    std::string BotDiagnostics::FormatBotStatus(Brain::BotBrain* brain, MovementManager* movement)
    {
        if (!brain)
            return EmitBotStatus("Bot has no active brain.");
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return EmitBotStatus("Bot is unavailable, not in world, or currently teleporting.");

        const auto& bb = brain->GetBlackboard();
            std::string goalStr = brain->GetGoalString();
            std::string actionStr = brain->GetActionString();
            std::string actionDetail = brain->GetActionDiagnosticDetail();
            std::string previousActionOutcome = brain->GetPreviousActionOutcome();
            bool explicitlyTraced = Diagnostics::BotTrace::IsEnabled(
                static_cast<uint32_t>(bot->GetGUID().GetCounter()));
            const char* traceStatus = explicitlyTraced ? "Enabled" :
                (Diagnostics::BotTrace::IsGlobalVerbose() ? "Global verbose" : "Disabled");
            const char* partyRole = "None";
            switch (bb.party.role)
            {
                case Party::Role::Tank: partyRole = "Tank"; break;
                case Party::Role::Healer: partyRole = "Healer"; break;
                case Party::Role::Damage: partyRole = "Damage"; break;
                default: break;
            }
    
            const char* movementState = movement ? movement->GetStateName() : "Unavailable";
            const char* externalMode = movement ? movement->GetExternalControlModeName() : "Unavailable";
            const char* hasPath = movement && movement->HasPath() ? "Yes" : "No";
            std::string destination = movement && movement->HasPath()
                ? fmt::format("({:.1f}, {:.1f}, {:.1f})", movement->GetDestinationX(),
                    movement->GetDestinationY(), movement->GetDestinationZ())
                : "None";
    
            const auto& metrics = brain->GetTransitionMetrics();
            return EmitBotStatus(fmt::format(
                "[WorldBots Status] {} (Level {})\n"
                " - Profile: {}\n"
                " - Goal: {} (Tier: {})\n"
                " - Action: {}\n"
                " - Transitions: total={} recent={}/15s interrupted={} recentInterrupted={}/15s contextRefreshes={} churn={}\n"
                " - Quest Context: {}\n"
                " - Current Action Detail: {}\n"
                " - Previous Action Outcome: {}\n"
                " - Death Recovery: {}s remaining\n"
                " - Logging: {}\n"
                " - Trace: {}\n"
                " - Movement: {} | Path: {} | External Control: {}\n"
                " - Movement Destination: {}\n"
                " - Party: {} | Role: {} | Members: {} | Shared Target: {} | Leader Quest Count: {}\n"
                " - Blackboard: Ready: {} | Generation: {} | Ages ms: self {} / combat {} / spatial {} / party {} / nav {} / inventory {} / quest {}\n"
                " - Blackboard Quests: Available: {} | Active: {} | Completed: {}\n"
                " - Position: Map {} at ({:.1f}, {:.1f}, {:.1f})",
                bot->GetName(), bot->GetLevel(),
                brain->GetBehaviorProfileName(), goalStr, Brain::GoalTierToString(brain->GetActiveTier()),
                actionStr,
                metrics.totalTransitions, metrics.recentTransitions, metrics.interruptedActions,
                metrics.recentInterruptedTransitions, metrics.contextRefreshes, metrics.IsChurning() ? "YES" : "no",
                brain->GetActiveQuestId() != 0 ? std::to_string(brain->GetActiveQuestId()) : "None",
                actionDetail.empty() ? "None" : actionDetail,
                previousActionOutcome.empty() ? "None" : previousActionOutcome,
                brain->GetDeathRecoveryRemainingSeconds(),
                Diagnostics::BotTrace::GetModeName(),
                traceStatus,
                movementState, hasPath, externalMode,
                destination,
                bb.party.isInGroup ? "Grouped" : "Solo", partyRole, bb.party.memberGuids.size(),
                bb.party.groupTargetGuid ? std::to_string(bb.party.groupTargetGuid.GetCounter()) : "None",
                bb.party.leaderQuestIds.size(),
                bb.initialSnapshotReady ? "Yes" : "No", bb.generation,
                bb.self.ageMs, bb.combat.ageMs, bb.spatial.ageMs, bb.party.ageMs,
                bb.nav.ageMs, bb.inv.ageMs, bb.quest.ageMs,
                bb.quest.availableQuests.size(), bb.quest.activeQuests.size(), bb.quest.completedQuests.size(),
                bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));
    }

    std::string BotDiagnostics::FormatVendorStatus(Brain::BotBrain* brain)
    {
        if (!brain)
            return EmitBotStatus("Bot has no active brain.");
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return EmitBotStatus("Bot is unavailable, not in world, or currently teleporting.");

        const auto& bb = brain->GetBlackboard();
            Town::Plan plan = brain->PreviewTownPlan();
            bool lowSpace = Helper::InventoryUtils::CountFreeBagSlots(bot) <= 3;
            Helper::InventoryPolicyContext inventoryPolicy =
                Helper::InventoryUtils::BuildPolicyContext(bot, lowSpace);
            uint32_t sellableStacks = 0;
            uint32_t discardableStacks = 0;
            uint32_t protectedStacks = 0;
            std::ostringstream items;
    
            Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8 bag, uint8 slot, Item* item) {
                ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
                Helper::InventoryItemDecision decision =
                    Helper::InventoryUtils::ClassifyForSpace(bot, item, inventoryPolicy);
                sellableStacks += decision.sell ? 1u : 0u;
                discardableStacks += decision.discardWhenFull ? 1u : 0u;
                protectedStacks += !decision.sell && !decision.discardWhenFull ? 1u : 0u;
                items << "\n   - Bag " << static_cast<uint32_t>(bag)
                      << " Slot " << static_cast<uint32_t>(slot) << ": "
                      << (proto ? proto->Name1 : "<unknown>")
                      << " x" << (item ? item->GetCount() : 0)
                      << " (Entry " << (proto ? proto->ItemId : 0)
                      << ", Quality " << (proto ? static_cast<uint32_t>(proto->Quality) : 0)
                      << ", Sell " << (proto ? proto->SellPrice : 0) << "c) -> "
                      << decision.reason;
                return true;
            });
    
            uint32_t freeSlots = Helper::InventoryUtils::CountFreeBagSlots(bot);
            uint32_t projectedFreeSlots = freeSlots + sellableStacks + discardableStacks;
            uint32_t requiredSlots = plan.targetFreeBagSlots;
            if (requiredSlots == 0 && (bb.inv.bagsFull || Actions::LootAction::HasInventoryBlockedLoot(bot)))
                requiredSlots = 1;
    
            Creature* liveVendor = Helper::NpcUtils::FindNearbyServiceNpc(
                bot, true, false, Constants::TacticalScanRadius);
            uint32_t cachedVendorEntry = bb.inv.nearestVendorEntry;
            uint32_t vendorSuppression = cachedVendorEntry != 0
                ? brain->GetNpcSuppressionRemainingSeconds(cachedVendorEntry) : 0;
            std::string cachedVendor = cachedVendorEntry != 0
                ? fmt::format("Entry {} at ({:.1f}, {:.1f}, {:.1f}) Map {} | Distance {:.1f}yd | Suppressed {}s",
                    cachedVendorEntry, bb.inv.vendorPosition.x, bb.inv.vendorPosition.y,
                    bb.inv.vendorPosition.z, bb.inv.vendorPosition.mapId,
                    Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                        bb.inv.vendorPosition.x, bb.inv.vendorPosition.y), vendorSuppression)
                : "None";
    
            std::ostringstream output;
            output << "[WorldBots Vendor Status] " << bot->GetName() << '\n'
                   << " - Goal / Action: " << brain->GetGoalString() << " / " << brain->GetActionString() << '\n'
                   << " - Progression: Level " << static_cast<uint32_t>(bot->GetLevel())
                   << " | Quest ceiling +" << Config::BotConfig::GetQuestMaxLevelsAboveBot()
                   << " | Grind range " << Config::BotConfig::GetGrindMinLevelOffset()
                   << " to " << Config::BotConfig::GetGrindMaxLevelOffset()
                   << " | Retry quests at level "
                   << (brain->GetGrindUntilLevel() ? std::to_string(brain->GetGrindUntilLevel()) : "immediately") << '\n'
                   << " - Bag Slots: " << freeSlots << '/' << bb.inv.totalBagSlots
                   << " free | Low: " << (bb.inv.lowBagSpace ? "Yes" : "No")
                   << " | Full: " << (bb.inv.bagsFull ? "Yes" : "No") << '\n'
                   << " - Policy Stacks: Sell " << sellableStacks
                   << " | Discard-if-full " << discardableStacks
                   << " | Protected " << protectedStacks << '\n'
                   << " - Cleanup Projection: " << freeSlots << " -> " << projectedFreeSlots
                   << " free | Target " << requiredSlots
                   << " | Can meet target: " << (projectedFreeSlots >= requiredSlots ? "Yes" : "No") << '\n'
                   << " - Blocked Loot Corpses: " << Actions::LootAction::GetInventoryBlockedLootCount(bot) << '\n'
                   << " - Cleanup Backoff: " << brain->GetInventoryCleanupRetryRemainingSeconds()
                   << "s remaining | Free slots when blocked: " << brain->GetInventoryCleanupBlockedFreeSlots() << '\n'
                   << " - Live Vendor Within 30yd: "
                   << (liveVendor ? fmt::format("{} (Entry {})", liveVendor->GetName(), liveVendor->GetEntry()) : "None") << '\n'
                   << " - Cached Vendor: " << cachedVendor << '\n'
                   << " - Town Plan: Target slots " << plan.targetFreeBagSlots
                   << " | Steps " << plan.steps.size()
                   << " | Missing vendor " << (plan.blockedByMissingVendor ? "Yes" : "No")
                   << " | Protected block " << (plan.blockedByProtectedInventory ? "Yes" : "No") << '\n'
                   << " - Inventory Decisions:" << items.str();
    
            return EmitBotStatus(output.str());
    }

    std::string BotDiagnostics::FormatQuestStatus(Brain::BotBrain* brain)
    {
        if (!brain)
            return EmitBotStatus("Bot has no active brain.");
        Player* bot = brain->GetBot();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return EmitBotStatus("Bot is unavailable, not in world, or currently teleporting.");

        const auto& bb = brain->GetBlackboard();
            std::ostringstream output;
            output << "[WorldBots Quest Status] " << bot->GetName() << '\n'
                   << " - Goal / Action: " << brain->GetGoalString() << " / " << brain->GetActionString() << '\n'
                   << " - Selected Quest: " << (brain->GetActiveQuestId() ? std::to_string(brain->GetActiveQuestId()) : "None") << '\n'
                   << " - Progress Watch: Quest " << brain->GetQuestProgressWatchId()
                   << " | Active-work elapsed " << brain->GetQuestProgressWatchElapsedMs() << "ms / 180000ms\n"
                   << " - Inventory: " << bb.inv.freeBagSlots << '/' << bb.inv.totalBagSlots
                   << " free | Blocked loot corpses " << Actions::LootAction::GetInventoryBlockedLootCount(bot)
                   << " | Cleanup backoff " << brain->GetInventoryCleanupRetryRemainingSeconds() << "s\n"
                   << " - Active Quests: " << bb.quest.activeQuests.size();
    
            for (const auto& quest : bb.quest.activeQuests)
            {
                Quest const* questTemplate = sObjectMgr->GetQuestTemplate(quest.questId);
                uint32_t suppressed = brain->GetQuestSuppressionRemainingSeconds(quest.questId);
                uint32_t retryLevel = brain->GetQuestRetryLevel(quest.questId);
                output << "\n   Quest " << quest.questId << " ('"
                       << (questTemplate ? questTemplate->GetTitle() : "Unknown") << "')"
                       << " | Level " << (questTemplate ? questTemplate->GetQuestLevel() : 0)
                       << " | " << (quest.questId == brain->GetActiveQuestId() ? "SELECTED" : "not selected")
                       << " | Suppressed " << suppressed << "s"
                       << " | Failures this level " << brain->GetQuestFailureCount(quest.questId)
                       << " | Retry "
                       << (brain->IsQuestSessionBlocked(quest.questId)
                            ? "blocked for session"
                            : (retryLevel > bot->GetLevel()
                                ? "at level " + std::to_string(retryLevel)
                                : "eligible"));
                if (quest.hasTargetPosition)
                {
                    output << "\n     Target: Map " << quest.targetPosition.mapId << " at ("
                           << fmt::format("{:.1f}, {:.1f}, {:.1f}", quest.targetPosition.x,
                               quest.targetPosition.y, quest.targetPosition.z)
                           << ") | Distance " << fmt::format("{:.1f}", Helper::Distance2D(
                               bot->GetPositionX(), bot->GetPositionY(),
                               quest.targetPosition.x, quest.targetPosition.y)) << "yd";
                }
                else
                    output << "\n     Target: unresolved";
    
                if (quest.objectives.empty())
                    output << "\n     Objectives: none resolved";
    
                for (const auto& objective : quest.objectives)
                {
                    output << "\n     - " << QuestObjectiveTypeName(objective.type)
                           << " Entry " << objective.targetEntry;
                    uint32_t interactionEntry = objective.interactionEntry != 0
                        ? objective.interactionEntry : objective.targetEntry;
                    if (interactionEntry != objective.targetEntry ||
                        objective.interactionKind != objective.targetKind)
                    {
                        output << " | Interaction "
                               << (objective.interactionKind == Blackboard::QuestTargetKind::GameObject
                                   ? "GameObject " : "Creature ")
                               << interactionEntry;
                    }
                    if (objective.sourceItemId != 0)
                    {
                        output << " | Source Item " << objective.sourceItemId;
                        if (objective.sourceSpellId != 0)
                            output << " Spell " << objective.sourceSpellId;
                    }
                    if (objective.itemId != 0)
                    {
                        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(objective.itemId);
                        output << " | Item " << objective.itemId << " ('"
                               << (itemTemplate ? itemTemplate->Name1 : "Unknown") << "')";
                    }
                    output << " | Progress " << objective.currentCount << '/' << objective.requiredCount;
    
                    if (objective.type == Blackboard::QuestObjectiveType::CollectItem &&
                        objective.itemId != 0 && objective.currentCount < objective.requiredCount)
                    {
                        ItemPosCountVec destination;
                        InventoryResult storeResult = bot->CanStoreNewItem(
                            NULL_BAG, NULL_SLOT, destination, objective.itemId, 1);
                        output << " | Store preflight: " << InventoryResultName(storeResult)
                               << " (" << static_cast<uint32_t>(storeResult) << ')';
                    }
                }
            }
    
            output << "\n - Completed Quests: " << bb.quest.completedQuests.size();
            for (const auto& quest : bb.quest.completedQuests)
            {
                Quest const* questTemplate = sObjectMgr->GetQuestTemplate(quest.questId);
                output << "\n   - " << quest.questId << " ('"
                       << (questTemplate ? questTemplate->GetTitle() : "Unknown") << "')"
                       << " | Turn-in position " << (quest.hasTurnInPosition ? "resolved" : "missing")
                       << " | Retry "
                       << (brain->IsQuestSessionBlocked(quest.questId)
                            ? "blocked for session"
                            : (brain->GetQuestRetryLevel(quest.questId) > bot->GetLevel()
                                ? "at level " + std::to_string(brain->GetQuestRetryLevel(quest.questId))
                                : "eligible"))
                       << " | Reward inventory blocked "
                       << (Helper::QuestUtils::IsRewardBlockedByInventory(bot, questTemplate) ? "Yes" : "No");
            }
    
            auto suppressedQuests = brain->GetSuppressedQuests();
            output << "\n - Suspended Quests: " << suppressedQuests.size();
            for (const auto& [questId, remaining] : suppressedQuests)
                output << "\n   - Quest " << questId << ": " << remaining << "s remaining";
    
            return EmitBotStatus(output.str());
    }
}
