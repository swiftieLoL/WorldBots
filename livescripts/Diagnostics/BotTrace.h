#pragma once

#include <cstdint>

class Player;

namespace Diagnostics
{
    class BotTrace
    {
    public:
        static void SetGlobalVerbose(bool enabled);
        static bool IsGlobalVerbose();

        static void SetEnabled(uint32_t guidLow, bool enabled);
        static bool IsEnabled(uint32_t guidLow);
        static bool ShouldLog(const Player* bot);
        static void Clear();
    };
}
