#pragma once

#include "ScheduledTask.h"
#include <vector>
#include <memory>
#include <string>

namespace Framework
{
    class Scheduler
    {
    public:
        Scheduler() = default;
        ~Scheduler() = default;

        void RegisterTask(std::shared_ptr<ScheduledTask> task);
        void RemoveTask(const std::string& taskName);
        void Clear();

        void Update(uint32_t diff);

    private:
        std::vector<std::shared_ptr<ScheduledTask>> _tasks;
    };
}
