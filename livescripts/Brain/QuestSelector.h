#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <limits>

namespace Brain
{
    struct QuestCandidate
    {
        uint32_t questId = 0;
        bool hasTargetPosition = false;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float targetZ = 0.0f;
        uint32_t targetMapId = 0;
        bool isActiveQuest = false;
        bool isCoordinatedQuest = false;
        uint32_t failureCount = 0;
    };

    /// Pure function for scoring and selecting the best quest from candidate list.
    std::optional<uint32_t> SelectBestQuest(
        const std::vector<QuestCandidate>& candidates,
        uint32_t botMapId,
        float botX, float botY, float botZ,
        uint64_t botSelectionKey = 0);
}
