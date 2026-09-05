#include "QuestSelector.h"
#include "Brain/QuestDistributionPolicy.h"
#include "Helper/MathUtils.h"
#include <cmath>
#include <limits>
#include <tuple>

namespace Brain
{
    std::optional<uint32_t> SelectBestQuest(
        const std::vector<QuestCandidate>& candidates,
        uint32_t botMapId,
        float botX, float botY, float botZ,
        uint64_t botSelectionKey)
    {
        if (candidates.empty())
            return std::nullopt;

        bool hasValidCandidate = false;
        uint32_t selectedQuestId = 0;
        std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint64_t,
            float, uint32_t> selectedScore;

        for (const auto& q : candidates)
        {
            float distanceSq = q.hasTargetPosition && q.targetMapId == botMapId
                ? Helper::DistanceSq(q.targetX, q.targetY, q.targetZ,
                    botX, botY, botZ)
                : 0.0f;
            uint32_t continuityRank = q.isCoordinatedQuest ? 0u
                : (q.isActiveQuest && q.failureCount == 0 && q.hasTargetPosition
                    ? 1u : (q.hasTargetPosition ? 2u : 3u));
            uint32_t mapRank = !q.hasTargetPosition ? 2u
                : (q.targetMapId == botMapId ? 0u : 1u);
            uint32_t distanceBand = q.hasTargetPosition &&
                q.targetMapId == botMapId
                ? static_cast<uint32_t>(std::sqrt(distanceSq) / 100.0f)
                : (q.hasTargetPosition ? 100u : 200u);
            uint64_t affinityRank = botSelectionKey == 0
                ? static_cast<uint64_t>(q.questId)
                : QuestDistributionHash(botSelectionKey, q.questId);
            auto score = std::make_tuple(continuityRank, q.failureCount,
                mapRank, distanceBand, affinityRank, distanceSq, q.questId);

            if (!hasValidCandidate || score < selectedScore)
            {
                hasValidCandidate = true;
                selectedQuestId = q.questId;
                selectedScore = score;
            }
        }

        if (hasValidCandidate)
            return selectedQuestId;

        return std::nullopt;
    }
}
