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
        // Callbacks may register, remove, or clear tasks. Keep a stable
        // snapshot for this update so vector mutations cannot skip a task,
        // and retain the executing task until its callback returns even when
        // the scheduler owns its last shared_ptr.
        std::vector<std::shared_ptr<ScheduledTask>> tasks = _tasks;
        for (const std::shared_ptr<ScheduledTask>& task : tasks)
        {
            if (task && task->IsActive())
                task->Update(diff);
        }
    }
}
