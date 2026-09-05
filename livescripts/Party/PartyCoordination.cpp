#include "PartyCoordination.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace Party
{
    namespace
    {
        constexpr uint8_t DeclaredTank = 0x02;
        constexpr uint8_t DeclaredHealer = 0x04;

        int TankScore(const MemberDescriptor& member)
        {
            if (member.declaredRoles & DeclaredHealer)
                return std::numeric_limits<int>::max();
            int score = (member.declaredRoles & DeclaredTank) ? -100 : 0;
            if (member.isLeader)
                score -= 5;
            switch (member.classId)
            {
                case 1: return score + 0;  // Warrior
                case 2: return score + 1;  // Paladin
                case 6: return score + 2;  // Death Knight
                case 11: return score + 3; // Druid
                default: return std::numeric_limits<int>::max();
            }
        }

        int HealerScore(const MemberDescriptor& member)
        {
            if (member.declaredRoles & DeclaredTank)
                return std::numeric_limits<int>::max();
            int score = (member.declaredRoles & DeclaredHealer) ? -100 : 0;
            switch (member.classId)
            {
                case 5: return score + 0;  // Priest
                case 7: return score + 1;  // Shaman
                case 11: return score + 2; // Druid
                case 2: return score + 3;  // Paladin
                default: return std::numeric_limits<int>::max();
            }
        }

        uint64_t SelectBest(const std::vector<MemberDescriptor>& members,
            int (*scoreFn)(const MemberDescriptor&), uint64_t excludedId = 0)
        {
            uint64_t selectedId = 0;
            int selectedScore = std::numeric_limits<int>::max();
            for (const MemberDescriptor& member : members)
            {
                if (!member.id || member.id == excludedId)
                    continue;
                int score = scoreFn(member);
                if (score < selectedScore || (score == selectedScore && member.id < selectedId))
                {
                    selectedId = member.id;
                    selectedScore = score;
                }
            }
            return selectedScore == std::numeric_limits<int>::max() ? 0 : selectedId;
        }
    }

    Role RoleAssignment::GetRole(uint64_t memberId) const
    {
        if (!memberId)
            return Role::None;
        if (memberId == tankId)
            return Role::Tank;
        if (memberId == healerId)
            return Role::Healer;
        return Role::Damage;
    }

    RoleAssignment AssignRoles(const std::vector<MemberDescriptor>& members)
    {
        RoleAssignment assignment;
        if (members.empty())
            return assignment;

        bool hasDeclaredTank = std::any_of(members.begin(), members.end(),
            [](const MemberDescriptor& m) { return (m.declaredRoles & DeclaredTank) != 0; });

        if (hasDeclaredTank)
        {
            assignment.tankId = SelectBest(members, TankScore);
            assignment.healerId = SelectBest(members, HealerScore, assignment.tankId);
        }
        else
        {
            assignment.healerId = SelectBest(members, HealerScore);
            assignment.tankId = SelectBest(members, TankScore, assignment.healerId);
        }
        return assignment;
    }

    FormationSlot ChooseFormation(Role role, size_t memberOrdinal)
    {
        constexpr float Pi = 3.14159265f;
        switch (role)
        {
            case Role::Tank:
                return { 2.5f, 0.0f };
            case Role::Healer:
                return { 7.0f, Pi };
            case Role::Damage:
            {
                constexpr float angles[] = { 2.15f, -2.15f, 1.25f, -1.25f };
                return { 4.5f + static_cast<float>(memberOrdinal % 2),
                    angles[memberOrdinal % std::size(angles)] };
            }
            case Role::None:
            default:
                return {};
        }
    }

    uint32_t SelectSharedQuest(const std::vector<uint32_t>& localQuestIds,
        const std::vector<uint32_t>& leaderQuestIds, uint32_t preferredQuestId)
    {
        auto contains = [](const std::vector<uint32_t>& questIds, uint32_t questId) {
            return std::find(questIds.begin(), questIds.end(), questId) != questIds.end();
        };
        if (preferredQuestId && contains(localQuestIds, preferredQuestId) &&
            contains(leaderQuestIds, preferredQuestId))
            return preferredQuestId;
        for (uint32_t questId : leaderQuestIds)
            if (contains(localQuestIds, questId))
                return questId;
        return 0;
    }
}
