#pragma once

#include <string>

class MovementManager;

namespace Brain
{
    class BotBrain;
}

namespace Commands
{
    class BotDiagnostics
    {
    public:
        static std::string FormatBotStatus(Brain::BotBrain* brain, MovementManager* movement);
        static std::string FormatVendorStatus(Brain::BotBrain* brain);
        static std::string FormatQuestStatus(Brain::BotBrain* brain);
    };
}

