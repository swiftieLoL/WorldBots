#pragma once

#include "IClassStrategy.h"
#include <cstdint>
#include <memory>

namespace Combat
{
    class ClassStrategyFactory
    {
    public:
        static std::unique_ptr<IClassStrategy> GetStrategyForClass(uint8_t clazz);
    };
}
