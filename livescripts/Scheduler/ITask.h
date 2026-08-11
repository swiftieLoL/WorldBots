#pragma once

#include <cstdint>

namespace Framework
{
    class ITask
    {
    public:
        virtual ~ITask() = default;
        virtual uint32_t GetIntervalMs() const = 0;
        virtual bool IsActive() const { return true; }
    };
}
