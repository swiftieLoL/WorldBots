#include "BotTrace.h"

#include "Globals/ObjectMgr.h"
#include "Player.h"

#include <unordered_set>

namespace Diagnostics
{
    namespace
    {
        bool s_globalVerbose = false;
        std::unordered_set<uint32_t> s_tracedBotGuids;
    }

    void BotTrace::SetGlobalVerbose(bool enabled)
    {
        s_globalVerbose = enabled;
    }

    bool BotTrace::IsGlobalVerbose()
    {
        return s_globalVerbose;
    }

    void BotTrace::SetEnabled(uint32_t guidLow, bool enabled)
    {
        if (guidLow == 0)
            return;

        if (enabled)
            s_tracedBotGuids.insert(guidLow);
        else
            s_tracedBotGuids.erase(guidLow);
    }

    bool BotTrace::IsEnabled(uint32_t guidLow)
    {
        return guidLow != 0 && s_tracedBotGuids.find(guidLow) != s_tracedBotGuids.end();
    }

    bool BotTrace::ShouldLog(const Player* bot)
    {
        return s_globalVerbose ||
            (bot && IsEnabled(static_cast<uint32_t>(bot->GetGUID().GetCounter())));
    }

    void BotTrace::Clear()
    {
        s_tracedBotGuids.clear();
    }
}
