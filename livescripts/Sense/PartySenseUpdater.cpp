#include "SenseUpdaters.h"
#include "Helper/InventoryUtils.h"
#include "Helper/SpellUtils.h"
#include "Party/PartyCoordination.h"
#include "ObjectAccessor.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Entities/Item/Item.h"
#include "Group.h"
#include "QuestDef.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Sense
{
    namespace
    {
        struct SharedPartySnapshot
        {
            std::chrono::steady_clock::time_point refreshedAt;
            ObjectGuid leaderGuid;
            std::vector<ObjectGuid> memberGuids;
            Party::RoleAssignment assignment;
            std::vector<uint32_t> leaderQuestIds;
            std::vector<uint32_t> leaderCompletedQuestIds;
            bool memberNeedsTownRun = false;
            ObjectGuid memberNeedingTownRunGuid;
        };

        std::mutex s_partySnapshotMutex;
        std::unordered_map<uint64_t, SharedPartySnapshot> s_partySnapshots;
        constexpr auto SharedPartySnapshotTtl = std::chrono::milliseconds(100);
        constexpr auto SharedPartySnapshotRetention = std::chrono::seconds(30);

        bool HasItemsNeedingRepair(Player* player)
        {
            if (!player)
                return false;

            auto needsRepair = [](Item* item) {
                return item && item->CalculateDurabilityRepairCost(1.0f) > 0;
            };

            // Match the core durability scan coverage without invoking a
            // mutating repair path.
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                if (needsRepair(player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
                    return true;
            }

            for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            {
                for (uint8 slot = 0; slot < MAX_BAG_SIZE; ++slot)
                {
                    if (needsRepair(player->GetItemByPos(bag, slot)))
                        return true;
                }
            }

            return false;
        }
    }


    void PartySenseUpdater::ClearSharedCache()
    {
        std::lock_guard<std::mutex> lock(s_partySnapshotMutex);
        s_partySnapshots.clear();
    }

    bool PartySenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        return Detail::ServiceSubstate(bb.party, deltaMs,
            [&]() { Refresh(bot, bb.party); });
    }

    void PartySenseUpdater::Refresh(Player* bot, Blackboard::PartyState& party)
    {
        party.memberGuids.clear();
        party.leaderQuestIds.clear();
        party.groupLeaderGuid.Clear();
        party.tankGuid.Clear();
        party.healerGuid.Clear();
        party.designatedResurrectorGuid.Clear();
        party.deadGroupMemberGuid.Clear();
        party.laggingQuestMemberGuid.Clear();
        party.lowestHealthGroupMemberGuid.Clear();
        party.groupTargetGuid.Clear();
        party.isInGroup = false;
        party.isGroupLeader = false;
        party.leaderOnSameMap = false;
        party.role = Party::Role::None;
        party.lowestHealthGroupMemberPct = 100;
        party.leaderDistance = 0.0f;
        party.formationDistance = 2.0f;
        party.formationAngle = 0.0f;
        party.laggingQuestId = 0;
        party.memberNeedsTownRun = false;
        party.memberNeedingTownRunGuid.Clear();

        if (!bot || !bot->IsInWorld()) return;

        Group* group = bot->GetGroup();
        if (!group) return;

        party.isInGroup = true;
        party.groupLeaderGuid = group->GetLeaderGUID();
        party.isGroupLeader = (group->GetLeaderGUID() == bot->GetGUID());

        SharedPartySnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(s_partySnapshotMutex);
            auto now = std::chrono::steady_clock::now();
            uint64_t groupKey = group->GetGUID().GetRawValue();
            auto snapshotIt = s_partySnapshots.find(groupKey);
            if (snapshotIt == s_partySnapshots.end() ||
                now - snapshotIt->second.refreshedAt >= SharedPartySnapshotTtl)
            {
                SharedPartySnapshot newSnapshot;
                newSnapshot.refreshedAt = now;
                newSnapshot.leaderGuid = group->GetLeaderGUID();
                std::vector<Party::MemberDescriptor> descriptors;
                descriptors.reserve(group->GetMembersCount());
                for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (!member || !member->IsInWorld())
                        continue;
                    uint8_t declaredRoles = 0;
                    for (const Group::MemberSlot& slot : group->GetMemberSlots())
                    {
                        if (slot.guid == member->GetGUID())
                        {
                            declaredRoles = slot.roles;
                            break;
                        }
                    }
                    newSnapshot.memberGuids.push_back(member->GetGUID());
                    descriptors.push_back({ member->GetGUID().GetRawValue(), member->GetClass(),
                        declaredRoles, member->GetGUID() == newSnapshot.leaderGuid });

                    if (member->IsAlive())
                    {
                        uint32 freeSlots = Helper::InventoryUtils::CountFreeBagSlots(member);
                        bool memberFullBags = (freeSlots <= 1);
                        bool memberNeedsRepair = HasItemsNeedingRepair(member);
                        if (!newSnapshot.memberNeedsTownRun && (memberFullBags || memberNeedsRepair))
                        {
                            newSnapshot.memberNeedsTownRun = true;
                            newSnapshot.memberNeedingTownRunGuid = member->GetGUID();
                        }
                    }
                }
                newSnapshot.assignment = Party::AssignRoles(descriptors);
                if (Player* leader = ObjectAccessor::FindPlayer(newSnapshot.leaderGuid))
                {
                    if (leader->IsInWorld() && !leader->IsBeingTeleported())
                    {
                        for (uint16_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                        {
                            uint32_t questId = leader->GetQuestSlotQuestId(slot);
                            QuestStatus status = questId ? leader->GetQuestStatus(questId) : QUEST_STATUS_NONE;
                            if (status == QUEST_STATUS_INCOMPLETE)
                                newSnapshot.leaderQuestIds.push_back(questId);
                            else if (status == QUEST_STATUS_COMPLETE)
                                newSnapshot.leaderCompletedQuestIds.push_back(questId);
                        }
                    }
                }
                snapshotIt = s_partySnapshots.insert_or_assign(groupKey, std::move(newSnapshot)).first;

                for (auto it = s_partySnapshots.begin(); it != s_partySnapshots.end(); )
                    it = now - it->second.refreshedAt > SharedPartySnapshotRetention
                        ? s_partySnapshots.erase(it) : std::next(it);
            }
            snapshot = snapshotIt->second;
        }

        party.memberGuids = snapshot.memberGuids;
        party.leaderQuestIds = snapshot.leaderQuestIds;
        party.memberNeedsTownRun = snapshot.memberNeedsTownRun;
        party.memberNeedingTownRunGuid = snapshot.memberNeedingTownRunGuid;
        std::vector<Player*> liveMembers;
        liveMembers.reserve(snapshot.memberGuids.size());
        float minDeadDist = std::numeric_limits<float>::max();
        for (ObjectGuid memberGuid : snapshot.memberGuids)
        {
            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (!member || !member->IsInWorld())
                continue;
            liveMembers.push_back(member);
            if (member->IsAlive() && member->GetMap() == bot->GetMap())
            {
                uint8_t hpPct = member->GetMaxHealth() > 0
                    ? static_cast<uint8_t>((member->GetHealth() * 100) / member->GetMaxHealth()) : 0;
                if (hpPct < party.lowestHealthGroupMemberPct)
                {
                    party.lowestHealthGroupMemberPct = hpPct;
                    party.lowestHealthGroupMemberGuid = member->GetGUID();
                }
            }
            else if (!member->IsAlive() && member != bot && member->GetMap() == bot->GetMap())
            {
                float dist = bot->GetDistance(member);
                if (dist < minDeadDist)
                {
                    minDeadDist = dist;
                    party.deadGroupMemberGuid = member->GetGUID();
                }
            }
        }

        Party::RoleAssignment assignment = snapshot.assignment;
        party.tankGuid = ObjectGuid(assignment.tankId);
        party.healerGuid = ObjectGuid(assignment.healerId);
        party.role = assignment.GetRole(bot->GetGUID().GetRawValue());
        size_t formationOrdinal = 0;
        for (size_t i = 0; i < party.memberGuids.size(); ++i)
            if (party.memberGuids[i] == bot->GetGUID())
                formationOrdinal = i;
        Party::FormationSlot formation = Party::ChooseFormation(party.role, formationOrdinal);
        party.formationDistance = formation.distance;
        party.formationAngle = formation.angle;

        Player* designatedResurrector = nullptr;
        for (Player* member : liveMembers)
        {
            uint32_t resurrectionSpell = member && member->IsAlive()
                ? Helper::SpellUtils::GetClassResurrectionSpell(member->GetClass()) : 0;
            if (!resurrectionSpell || member->GetMap() != bot->GetMap() ||
                Helper::SpellUtils::FindHighestKnownRank(member, resurrectionSpell) == 0)
                continue;
            bool memberIsHealer = member->GetGUID() == party.healerGuid;
            bool selectedIsHealer = designatedResurrector && designatedResurrector->GetGUID() == party.healerGuid;
            if (!designatedResurrector || (memberIsHealer && !selectedIsHealer) ||
                (memberIsHealer == selectedIsHealer && member->GetGUID() < designatedResurrector->GetGUID()))
                designatedResurrector = member;
        }
        if (designatedResurrector)
            party.designatedResurrectorGuid = designatedResurrector->GetGUID();

        if (Player* leader = ObjectAccessor::FindPlayer(party.groupLeaderGuid))
        {
            party.leaderOnSameMap = leader->IsInWorld() && !leader->IsBeingTeleported() && (leader->GetMap() == bot->GetMap());
            if (party.leaderOnSameMap)
                party.leaderDistance = bot->GetDistance(leader);

            if (party.isGroupLeader && !party.laggingQuestMemberGuid)
            {
                for (uint32_t questId : snapshot.leaderCompletedQuestIds)
                {
                    for (Player* member : liveMembers)
                    {
                        if (member != leader &&
                            member->GetMap() == bot->GetMap() &&
                            member->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                        {
                            party.laggingQuestId = questId;
                            party.laggingQuestMemberGuid = member->GetGUID();
                            break;
                        }
                    }
                    if (party.laggingQuestMemberGuid)
                        break;
                }
            }

            if (Unit* victim = leader->GetVictim())
            {
                if (victim->IsAlive() && victim->GetMap() == bot->GetMap() && bot->GetDistance(victim) <= 80.0f)
                    party.groupTargetGuid = victim->GetGUID();
            }
        }

        auto considerVictim = [&](Player* member) {
            if (party.groupTargetGuid || !member || member->GetMap() != bot->GetMap())
                return;
            Unit* victim = member->GetVictim();
            if (victim && victim->IsAlive() && bot->GetDistance(victim) <= 80.0f)
                party.groupTargetGuid = victim->GetGUID();
        };
        considerVictim(ObjectAccessor::FindPlayer(party.tankGuid));
        for (Player* member : liveMembers)
            considerVictim(member);
        if (!party.groupTargetGuid)
        {
            for (Player* member : liveMembers)
            {
                if (!member || member->GetMap() != bot->GetMap())
                    continue;
                for (Unit* attacker : member->getAttackers())
                {
                    if (attacker && attacker->IsInWorld() && attacker->IsAlive() &&
                        attacker->GetMap() == bot->GetMap() && bot->GetDistance(attacker) <= 80.0f)
                    {
                        party.groupTargetGuid = attacker->GetGUID();
                        break;
                    }
                }
                if (party.groupTargetGuid)
                    break;
            }
        }
    }

}
