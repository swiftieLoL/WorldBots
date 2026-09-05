#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Brain
{
    template <typename IsEventActivePredicate>
    bool IsEventControlledSpawnActive(const std::vector<int16_t>& eventEntries,
        IsEventActivePredicate isEventActive)
    {
        if (eventEntries.empty())
            return true;

        for (int16_t eventEntry : eventEntries)
        {
            if (eventEntry == 0)
                return true;
            uint16_t eventId = static_cast<uint16_t>(
                eventEntry > 0 ? eventEntry : -eventEntry);
            if ((eventEntry > 0 && isEventActive(eventId)) ||
                (eventEntry < 0 && !isEventActive(eventId)))
            {
                return true;
            }
        }
        return false;
    }

    // A nearby quest scan must not hide a cached world-starter candidate that
    // an active AcceptQuestAction may already be travelling toward.
    template <typename QuestCandidate>
    void PublishCachedQuestCandidate(std::vector<QuestCandidate>& candidates,
        const QuestCandidate& cachedCandidate, bool cachedCandidateEligible)
    {
        if (!cachedCandidateEligible || cachedCandidate.questId == 0)
            return;

        bool alreadyPublished = std::any_of(candidates.begin(), candidates.end(),
            [questId = cachedCandidate.questId](const QuestCandidate& candidate) {
                return candidate.questId == questId;
            });
        if (!alreadyPublished)
            candidates.push_back(cachedCandidate);
    }
}
