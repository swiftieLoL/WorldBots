#pragma once

#include "ITask.h"
#include <functional>
#include <memory>
#include <string>

namespace Framework
{
    class ScheduledTask : public ITask
    {
    public:
        using TaskCallback = std::function<void(uint32_t)>;

        ScheduledTask(std::string name, uint32_t intervalMs, TaskCallback callback, uint32_t maxCatchUpExecutions = 4)
            : _name(std::move(name)), _intervalMs(intervalMs), _elapsedMs(0),
              _callback(std::move(callback)), _active(true),
              _maxCatchUpExecutions(maxCatchUpExecutions == 0 ? 1 : maxCatchUpExecutions)
        {
        }

        uint32_t GetIntervalMs() const override { return _intervalMs; }
        const std::string& GetName() const { return _name; }
        bool IsActive() const override { return _active; }
        void SetActive(bool active) { _active = active; }

        void Update(uint32_t diff)
        {
            if (!_active || _intervalMs == 0) return;

            _elapsedMs += diff;
            uint32_t executions = 0;
            while (_elapsedMs >= _intervalMs && executions < _maxCatchUpExecutions)
            {
                _elapsedMs -= _intervalMs;
                ++executions;

                if (_callback)
                {
                    _callback(_intervalMs);
                }
            }

            // Preserve phase alignment, but drop an unbounded backlog after a
            // long server hitch. This prevents the scheduler from turning one
            // lag spike into a second main-thread spike.
            if (_elapsedMs >= _intervalMs)
                _elapsedMs %= _intervalMs;
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
