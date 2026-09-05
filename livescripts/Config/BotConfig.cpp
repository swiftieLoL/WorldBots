#include "BotConfig.h"

#include "Log.h"

#include <algorithm>
#include <cstdlib>

#ifndef WORLDBOTS_MODULE_CONFIG_FILE
#define WORLDBOTS_MODULE_CONFIG_FILE "config/worldbots.conf"
#endif

namespace Config
{
    std::vector<Factory::BotDefinition> BotConfig::GetBotRoster()
    {
        std::vector<std::string> errors;
        std::vector<Factory::BotDefinition> roster = Factory::ParseBotRoster(
            sConfigMgr->GetStringDefault("WorldBots.Roster", "", true), &errors);
        for (const std::string& error : errors)
            TC_LOG_ERROR("server", "[WorldBots] [Config] Ignoring invalid {}.", error);

        const size_t beforeFilter = roster.size();
        roster.erase(std::remove_if(roster.begin(), roster.end(),
            [](const Factory::BotDefinition& definition) {
                return !BotConfig::IsBotClassAllowed(definition.playerClass);
            }), roster.end());
        if (roster.size() != beforeFilter)
        {
            TC_LOG_WARN("server", "[WorldBots] [Config] Ignoring {} Death Knight roster entr{} because WorldBots.EnableDeathKnights is disabled.",
                beforeFilter - roster.size(), beforeFilter - roster.size() == 1 ? "y" : "ies");
        }
        return roster;
    }

    bool BotConfig::LoadModuleRuntimeConfig(bool forceReload)
    {
        static bool attempted = false;
        static bool loaded = false;
        if (attempted && !forceReload)
            return loaded;

        attempted = true;
        const char* environmentPath = std::getenv("WORLDBOTS_CONFIG_FILE");
        const char* configPath = environmentPath && *environmentPath
            ? environmentPath : WORLDBOTS_MODULE_CONFIG_FILE;
        std::string error;
        loaded = sConfigMgr->LoadAdditionalFile(configPath, true, error);
        if (loaded)
        {
            TC_LOG_INFO("server", "[WorldBots] [Config] Loaded module-local runtime configuration from '{}'.",
                configPath);
        }
        else
        {
            TC_LOG_ERROR("server", "[WorldBots] [Config] Failed to load module-local runtime configuration '{}': {}",
                configPath, error.empty() ? "unknown configuration error" : error);
        }
        return loaded;
    }
}
