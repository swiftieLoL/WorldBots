#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

class Player;

namespace Party
{
    constexpr uint8_t MaxGroupSize = 5;
    constexpr uint32_t StruggleDeathThreshold = 1;
    constexpr uint32_t StruggleFleeThreshold = 2;
    constexpr float ProximityRadius = 500.0f;
    constexpr float LogSearchMaxRadius = 1500.0f;

    struct CandidateDescriptor
    {
        uint64_t guid = 0;
        uint32_t level = 0;
        uint8_t classId = 0;
        float distance = 0.0f;
        bool hasQuestInLog = false; // Tier 1
        bool canAcceptQuest = false; // Tier 2
        bool isSolo = true;
        uint8_t currentGroupSize = 1;

        bool operator==(const CandidateDescriptor&) const = default;
    };

    // Candidate scoring function:
    // Tier 1: Candidate already has the quest in their log: +1000 base score
    // Tier 2: Candidate is in proximity and can accept/share the quest: +500 base score
    // Bonus for proximity: closer candidates score higher
    // Bonus for level proximity: closer level scores higher
    // Bonus for party role diversity (e.g. Healer/Tank class paired with Damage class)
    int32_t ScoreCandidate(const CandidateDescriptor& c, uint32_t botLevel, uint8_t recruiterClass = 0);

    // Sorts candidate descriptors in descending order of priority score
    std::vector<CandidateDescriptor> RankCandidates(std::vector<CandidateDescriptor> candidates,
        uint32_t botLevel, uint8_t recruiterClass = 0);

    // Group continuity evaluator:
    // Determines if the group should stay together after turning in a quest
    bool ShouldKeepGroupTogether(bool hasSharedActiveQuest, bool hasNearbySharedChainQuest,
        uint32_t groupMemberCount = 2);

    // Runtime recruitment and disband helpers (defined in PartyRecruitmentPolicy.cpp)
    namespace PartyRecruitmentPolicy
    {
        // Scans the current map for candidate bots, ranks them, and recruits up to MaxGroupSize.
        // Returns the number of new members added.
        uint32_t TryRecruitForQuest(Player* bot, uint32_t questId);

        // Disbands the party if no shared active quests or chain follow-ups remain.
        // Returns true if the party was disbanded.
        bool CheckAndDisbandIfCompleted(Player* bot, uint32_t turnedInQuestId);
    }
}
