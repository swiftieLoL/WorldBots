#pragma once

#include "Town/TownPlanning.h"

#include <string>

namespace Testing
{
    class ScenarioRunner
    {
    public:
        static std::string ListScenarios();
        static std::string RunLogicScenarios();
        static std::string DescribeTownPlan(const std::string& botName, const Town::Plan& plan);
    };
}
