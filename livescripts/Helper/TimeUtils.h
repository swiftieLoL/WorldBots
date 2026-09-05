#pragma once

#include <chrono>
#include <cstdint>

namespace Helper
{
    inline uint64_t MonotonicMilliseconds()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // Process-relative seconds are suitable for retry windows and negative
    // caches: unlike wall-clock time, they cannot jump when the host clock is
    // synchronized or manually changed.
    inline uint32_t MonotonicSeconds()
    {
        return static_cast<uint32_t>(MonotonicMilliseconds() / 1000);
    }
}
