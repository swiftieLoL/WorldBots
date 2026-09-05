#pragma once

#include <algorithm>
#include <cstdint>

namespace Brain
{
    // Keeps an action alive through short gaps in a periodically refreshed
    // sensor. A positive observation resets the gap; a continuous absence
    // eventually expires so genuinely stale work can still be replanned.
    class ObservationGrace
    {
    public:
        explicit ObservationGrace(uint32_t graceMs) : _graceMs(graceMs) { }

        void Reset() { _missingMs = 0; }

        bool Observe(bool present, uint32_t deltaMs)
        {
            if (present)
            {
                Reset();
                return true;
            }

            _missingMs += std::min(deltaMs, _graceMs - std::min(_missingMs, _graceMs));
            return _missingMs < _graceMs;
        }

        uint32_t GetMissingMs() const { return _missingMs; }

    private:
        uint32_t _graceMs = 0;
        uint32_t _missingMs = 0;
    };
}
