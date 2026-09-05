#pragma once

#include <cstddef>
#include <cstdint>

namespace Brain
{
    inline uint64_t QuestDistributionHash(uint64_t botKey, uint32_t questId,
        uint32_t objectiveEntry = 0)
    {
        uint64_t value = botKey ^ (static_cast<uint64_t>(questId) << 32) ^
            static_cast<uint64_t>(objectiveEntry);
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return value;
    }

    inline std::size_t SelectDistributedCandidateIndex(std::size_t candidateCount,
        uint64_t botKey, uint32_t questId, uint32_t objectiveEntry = 0)
    {
        return candidateCount == 0 ? 0 : static_cast<std::size_t>(
            QuestDistributionHash(botKey, questId, objectiveEntry) % candidateCount);
    }

    inline uint64_t QuestLocationAffinity(uint64_t botKey, uint32_t questId,
        uint32_t objectiveEntry, uint32_t mapId, int32_t quantizedX,
        int32_t quantizedY)
    {
        uint64_t locationKey = (static_cast<uint64_t>(mapId) << 48) ^
            (static_cast<uint64_t>(static_cast<uint32_t>(quantizedX)) << 24) ^
            static_cast<uint32_t>(quantizedY);
        return QuestDistributionHash(botKey ^ locationKey, questId,
            objectiveEntry);
    }
}
