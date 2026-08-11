#include "Scheduler.h"
#include <algorithm>

namespace Framework
{
    void Scheduler::RegisterTask(std::shared_ptr<ScheduledTask> task)
    {
        if (task)
        {
            RemoveTask(task->GetName());
            _tasks.push_back(task);
        }
    }

    void Scheduler::RemoveTask(const std::string& taskName)
    {
        _tasks.erase(
            std::remove_if(_tasks.begin(), _tasks.end(),
                [&taskName](const std::shared_ptr<ScheduledTask>& t) { return t && t->GetName() == taskName; }),
            _tasks.end()
        );
    }

    void Scheduler::Clear()
    {
        _tasks.clear();
    }

    void Scheduler::Update(uint32_t diff)
    {
        for (auto& task : _tasks)
        {
            if (task && task->IsActive())
            {
                task->Update(diff);
            }
        }
    }
}
