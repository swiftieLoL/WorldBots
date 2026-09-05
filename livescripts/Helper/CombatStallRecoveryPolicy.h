#pragma once

#include <cstdint>

namespace Helper
{
    class CombatStallRecoveryPolicy
    {
    public:
        static constexpr std::uint32_t MaxExactTargetRelocations = 2;

        void Reset()
        {
            _exactTargetRelocations = 0;
        }

        // Exact-target relocation is a cheap first recovery for range or
        // navmesh-layer mistakes. Repeating it indefinitely cannot resolve an
        // evading or otherwise non-executable target, so the next stall must
        // escalate to the brain's safe-hub recovery.
        bool ShouldEscalateToSafeHub()
        {
            if (_exactTargetRelocations >= MaxExactTargetRelocations)
                return true;

            ++_exactTargetRelocations;
            return false;
        }

        std::uint32_t GetExactTargetRelocations() const
        {
            return _exactTargetRelocations;
        }

    private:
        std::uint32_t _exactTargetRelocations = 0;
    };
}
