#pragma once

#include <string>
#include <functional>

namespace Brain { class BotBrain; }

namespace Commands
{
    class BotCommands
    {
    public:
        using BrainResolver = std::function<Brain::BotBrain*(std::string const&)>;

        static std::string Handle(std::string const& command);
        static std::string RunTest(std::string const& arguments, BrainResolver const& resolveBrain);
        static std::string RunTrace(std::string const& arguments, BrainResolver const& resolveBrain);
    };
}
