#include "PartyRecruitmentPolicy.h"

#include <cmath>
#include <algorithm>
#include <utility>

namespace Party
{
    int32_t ScoreCandidate(const CandidateDescriptor& c, uint32_t botLevel, uint8_t recruiterClass)
    {
        if (!c.hasQuestInLog && !c.canAcceptQuest)
            return -1;
        if (!c.isSolo && c.currentGroupSize >= MaxGroupSize)
            return -1;

        // Tier 1 (quest already in log) vs Tier 2 (proximity eligibility)
        int32_t score = c.hasQuestInLog ? 1000 : 500;

        // Distance penalty: closer candidates are preferred
        int32_t distPenalty = std::min<int32_t>(300, static_cast<int32_t>(c.distance / 2.0f));
        score -= distPenalty;

        // Level proximity penalty (30 points per level difference)
        int32_t levelDelta = std::abs(static_cast<int32_t>(c.level) - static_cast<int32_t>(botLevel));
        score -= (levelDelta * 30);

        // Solo preference: solo bots don't bring group baggage
        if (c.isSolo)
            score += 50;

        // Class / role synergy
        auto isTankOrHealer = [](uint8_t cls) {
            return cls == 1 || cls == 2 || cls == 5 || cls == 6 || cls == 7 || cls == 11;
        };
        auto isDps = [](uint8_t cls) {
            return cls == 3 || cls == 4 || cls == 8 || cls == 9;
        };

        if (recruiterClass != 0)
        {
            if (isDps(recruiterClass) && isTankOrHealer(c.classId))
                score += 60;
            else if (isTankOrHealer(recruiterClass) && isDps(c.classId))
                score += 40;
            else if (recruiterClass != c.classId)
                score += 20;
        }

        return score;
    }

    std::vector<CandidateDescriptor> RankCandidates(std::vector<CandidateDescriptor> candidates,
        uint32_t botLevel, uint8_t recruiterClass)
    {
        std::vector<std::pair<int32_t, CandidateDescriptor>> scored;
        scored.reserve(candidates.size());

        for (const auto& c : candidates)
        {
            int32_t s = ScoreCandidate(c, botLevel, recruiterClass);
            if (s >= 0)
                scored.push_back({ s, c });
        }

        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first)
                return a.first > b.first;
            return a.second.distance < b.second.distance;
        });

        std::vector<CandidateDescriptor> result;
        result.reserve(scored.size());
        for (auto& p : scored)
            result.push_back(std::move(p.second));
        return result;
    }

    bool ShouldKeepGroupTogether(bool hasSharedActiveQuest, bool hasNearbySharedChainQuest,
        uint32_t groupMemberCount)
    {
        if (groupMemberCount <= 1)
            return false;
        return hasSharedActiveQuest || hasNearbySharedChainQuest;
    }
}

#if !defined(WORLDBOTS_LOGIC_TESTS)

#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Auth/BotAuth.h"
#include "Helper/MathUtils.h"
#include "Log.h"

#include <unordered_map>

namespace Party::PartyRecruitmentPolicy
{
    uint32_t TryRecruitForQuest(Player* bot, uint32_t questId)
    {
        if (!bot || !bot->IsInWorld() || questId == 0)
            return 0;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            return 0;

        Group* group = bot->GetGroup();
        if (group)
        {
            if (!group->IsLeader(bot->GetGUID()))
                return 0;
            if (group->GetMembersCount() >= MaxGroupSize)
                return 0;
        }

        uint8_t currentSize = group ? group->GetMembersCount() : 1;
        uint8_t needed = MaxGroupSize - currentSize;
        if (needed == 0)
            return 0;

        std::vector<CandidateDescriptor> candidates;
        std::unordered_map<uint64_t, Player*> candidatePlayers;

        Map* map = bot->GetMap();
        if (!map)
            return 0;

        for (auto itr = map->GetPlayers().begin(); itr != map->GetPlayers().end(); ++itr)
        {
            Player* other = itr->GetSource();
            if (!other || other == bot || !other->IsInWorld() || !other->IsAlive())
                continue;
            if (other->GetMapId() != bot->GetMapId() || other->GetTeam() != bot->GetTeam())
                continue;
            if (other->IsInFlight())
                continue;

            // Only bot-to-bot grouping
            if (BotAuth::GetSessionOwnership(other->GetGUID()) == BotAuth::SessionOwnership::None)
                continue;

            int32_t levelDelta = std::abs(static_cast<int32_t>(other->GetLevel()) - static_cast<int32_t>(bot->GetLevel()));
            if (levelDelta > 3)
                continue;

            Group* otherGroup = other->GetGroup();
            if (otherGroup != nullptr)
            {
                if (otherGroup == group)
                    continue;
                continue; // Skip players already in a different group
            }

            float dist = Helper::Distance2D(bot->GetPositionX(), bot->GetPositionY(),
                other->GetPositionX(), other->GetPositionY());

            bool hasInLog = (other->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE);
            bool canTake = (!hasInLog && other->GetQuestStatus(questId) == QUEST_STATUS_NONE &&
                other->CanTakeQuest(quest, false) && other->CanAddQuest(quest, false));

            if (!hasInLog && !canTake)
                continue;

            // Enforce max radius: Tier 1 allowed up to LogSearchMaxRadius (1500yd), Tier 2 up to ProximityRadius (500yd)
            if (hasInLog && dist > LogSearchMaxRadius)
                continue;
            if (!hasInLog && dist > ProximityRadius)
                continue;

            CandidateDescriptor cd;
            cd.guid = other->GetGUID().GetRawValue();
            cd.level = other->GetLevel();
            cd.classId = other->GetClass();
            cd.distance = dist;
            cd.hasQuestInLog = hasInLog;
            cd.canAcceptQuest = canTake;
            cd.isSolo = (otherGroup == nullptr);
            cd.currentGroupSize = otherGroup ? otherGroup->GetMembersCount() : 1;

            candidates.push_back(cd);
            candidatePlayers[cd.guid] = other;
        }

        if (candidates.empty())
            return 0;

        auto ranked = RankCandidates(std::move(candidates), bot->GetLevel(), bot->GetClass());
        uint32_t recruited = 0;

        for (const auto& cd : ranked)
        {
            if (recruited >= needed)
                break;

            auto pitr = candidatePlayers.find(cd.guid);
            if (pitr == candidatePlayers.end())
                continue;
            Player* candPlayer = pitr->second;
            if (!candPlayer || !candPlayer->IsInWorld() || !candPlayer->IsAlive())
                continue;
            if (candPlayer->GetGroup() != nullptr)
                continue;

            if (!group)
            {
                group = new Group();
                if (!group->Create(bot))
                {
                    delete group;
                    group = nullptr;
                    break;
                }
                sGroupMgr->AddGroup(group);
            }

            if (group->AddMember(candPlayer))
            {
                if (!cd.hasQuestInLog && candPlayer->CanTakeQuest(quest, false) && candPlayer->CanAddQuest(quest, false))
                {
                    candPlayer->AddQuestAndCheckCompletion(quest, nullptr);
                }
                recruited++;
                TC_LOG_INFO("server", "[WorldBots] [Party] Bot '{}' recruited bot '{}' for quest {} (Tier: {}, Group size: {})",
                    bot->GetName(), candPlayer->GetName(), questId, cd.hasQuestInLog ? "In-Log" : "Proximity-Eligible", group->GetMembersCount());
            }
        }

        if (group && recruited > 0)
        {
            group->BroadcastGroupUpdate();
        }

        return recruited;
    }

    bool CheckAndDisbandIfCompleted(Player* bot, uint32_t turnedInQuestId)
    {
        if (!bot || !bot->IsInWorld())
            return false;

        Group* group = bot->GetGroup();
        if (!group)
            return false;

        if (!group->IsLeader(bot->GetGUID()))
            return false;

        // Check if there are other shared active quests between members
        bool hasSharedActiveQuest = false;
        std::vector<Player*> liveMembers;
        for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m->IsInWorld())
                liveMembers.push_back(m);
        }

        for (uint16_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32_t qId = bot->GetQuestSlotQuestId(slot);
            if (qId == 0 || qId == turnedInQuestId)
                continue;
            if (bot->GetQuestStatus(qId) != QUEST_STATUS_INCOMPLETE)
                continue;

            for (Player* m : liveMembers)
            {
                if (m != bot && m->GetQuestStatus(qId) == QUEST_STATUS_INCOMPLETE)
                {
                    hasSharedActiveQuest = true;
                    break;
                }
            }
            if (hasSharedActiveQuest)
                break;
        }

        // Check if there is a next quest in chain
        bool hasChainFollowUp = false;
        if (turnedInQuestId != 0)
        {
            if (Quest const* currentQuest = sObjectMgr->GetQuestTemplate(turnedInQuestId))
            {
                uint32_t nextQuestId = currentQuest->GetNextQuestInChain();
                if (!nextQuestId)
                    nextQuestId = currentQuest->GetNextQuestId();
                if (nextQuestId != 0)
                {
                    if (Quest const* nextQ = sObjectMgr->GetQuestTemplate(nextQuestId))
                    {
                        if (bot->CanTakeQuest(nextQ, false))
                            hasChainFollowUp = true;
                    }
                }
            }
        }

        if (ShouldKeepGroupTogether(hasSharedActiveQuest, hasChainFollowUp, group->GetMembersCount()))
        {
            TC_LOG_INFO("server", "[WorldBots] [Party] Bot '{}' completed quest {} but has further shared/chain objectives; keeping party active (Members: {}).",
                bot->GetName(), turnedInQuestId, group->GetMembersCount());
            return false;
        }

        TC_LOG_INFO("server", "[WorldBots] [Party] Bot '{}' completed quest {} with no further shared objectives; disbanding party.",
            bot->GetName(), turnedInQuestId);
        group->Disband();
        return true;
    }
}
#endif
