#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <algorithm>

namespace Framework
{
    class ScheduledTask
    {
    public:
        using TaskCallback = std::function<void(uint32_t)>;

        ScheduledTask(std::string name, uint32_t intervalMs, TaskCallback callback, uint32_t maxCatchUpExecutions = 4)
            : _name(std::move(name)), _intervalMs(intervalMs), _elapsedMs(0),
              _callback(std::move(callback)), _active(true),
              _maxCatchUpExecutions(maxCatchUpExecutions == 0 ? 1 : maxCatchUpExecutions)
        {
        }

        const std::string& GetName() const { return _name; }
        bool IsActive() const { return _active; }

        void Update(uint32_t diff)
        {
            if (!_active || _intervalMs == 0) return;

            _elapsedMs += diff;
            if (_elapsedMs >= _intervalMs)
            {
                uint32_t executions = std::min(_elapsedMs / _intervalMs, _maxCatchUpExecutions);
                uint32_t stepMs = executions * _intervalMs;
                _elapsedMs -= stepMs;

                if (_callback)
                {
                    _callback(stepMs);
                }

                // Preserve phase alignment, but drop an unbounded backlog after a
                // long server hitch. This prevents the scheduler from turning one
                // lag spike into a second main-thread spike.
                if (_elapsedMs >= _intervalMs)
                    _elapsedMs %= _intervalMs;
            }
        }

    private:
        std::string _name;
        uint32_t _intervalMs;
        uint32_t _elapsedMs;
        TaskCallback _callback;
        bool _active;
        uint32_t _maxCatchUpExecutions;
    };
}
