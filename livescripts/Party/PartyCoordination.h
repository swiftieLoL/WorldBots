#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Party
{
    enum class Role : uint8_t
    {
        None = 0,
        Tank,
        Healer,
        Damage
    };

    struct MemberDescriptor
    {
        uint64_t id = 0;
        uint8_t classId = 0;
        uint8_t declaredRoles = 0;
        bool isLeader = false;

        bool operator==(const MemberDescriptor&) const = default;
    };

    struct RoleAssignment
    {
        uint64_t tankId = 0;
        uint64_t healerId = 0;

        Role GetRole(uint64_t memberId) const;
    };

    struct FormationSlot
    {
        float distance = 2.0f;
        float angle = 0.0f;

        bool operator==(const FormationSlot&) const = default;
    };

    RoleAssignment AssignRoles(const std::vector<MemberDescriptor>& members);
    FormationSlot ChooseFormation(Role role, size_t memberOrdinal);
    uint32_t SelectSharedQuest(const std::vector<uint32_t>& localQuestIds,
        const std::vector<uint32_t>& leaderQuestIds, uint32_t preferredQuestId = 0);
}
