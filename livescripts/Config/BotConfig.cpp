#include "BotConfig.h"

#include "Log.h"

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
        return roster;
    }

    bool BotConfig::LoadModuleRuntimeConfig()
    {
        static bool attempted = false;
        static bool loaded = false;
        if (attempted)
            return loaded;

        attempted = true;
        std::string error;
        loaded = sConfigMgr->LoadAdditionalFile(WORLDBOTS_MODULE_CONFIG_FILE, true, error);
        if (loaded)
        {
            TC_LOG_INFO("server", "[WorldBots] [Config] Loaded module-local runtime configuration from '{}'.",
                WORLDBOTS_MODULE_CONFIG_FILE);
        }
        else
        {
            TC_LOG_ERROR("server", "[WorldBots] [Config] Failed to load module-local runtime configuration '{}': {}",
                WORLDBOTS_MODULE_CONFIG_FILE, error.empty() ? "unknown configuration error" : error);
        }
        return loaded;
    }
}
