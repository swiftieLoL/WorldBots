#pragma once

#include <cstdint>

namespace Actions::LootCapacityRetryPolicy
{
    struct BlockedTarget
    {
        std::uint32_t expirySec = 0;
        std::uint32_t freeSlotsAtFailure = 0;
    };

    inline bool IsResolved(const BlockedTarget& target,
        std::uint32_t nowSec, std::uint32_t currentFreeSlots)
    {
        return nowSec >= target.expirySec ||
            currentFreeSlots > target.freeSlotsAtFailure;
    }
}
