#pragma once

#include "Config.h"
#include "Factory/BotRoster.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Config
{
    struct BotConfig
    {
        static bool LoadModuleRuntimeConfig();

        static bool IsEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.Enable", true, true);
        }

        static uint32_t GetBotCount()
        {
            int32_t configuredCount = sConfigMgr->GetIntDefault("WorldBots.BotCount", 3, true);
            if (configuredCount <= 0)
                return 0;
            uint32_t count = static_cast<uint32_t>(configuredCount);
            return count > GetMaxBotCount() ? GetMaxBotCount() : count;
        }

        static uint32_t GetMaxBotCount()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.MaxBotCount", 2000, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 2000;
        }

        static bool IsDebugModeEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.DebugMode", true, true);
        }

        static bool IsVerboseLoggingEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.VerboseLogging", false, true);
        }

        static bool AreTestsEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.Tests.Enable", false, true);
        }

        static uint32_t GetBotAccountId()
        {
            int32_t configuredAccountId = sConfigMgr->GetIntDefault("WorldBots.AccountId", 1, true);
            return configuredAccountId > 0 ? static_cast<uint32_t>(configuredAccountId) : 1;
        }

        static bool UseDedicatedAccounts()
        {
            std::string mode = sConfigMgr->GetStringDefault("WorldBots.AccountMode", "dedicated", true);
            return mode != "shared" && mode != "SHARED" && mode != "Shared";
        }

        static bool AutoCreateDedicatedAccounts()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.AutoCreateAccounts", true, true);
        }

        static bool ShouldMigrateLegacyCharacters()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.MigrateLegacyCharacters", true, true);
        }

        static std::string GetDedicatedAccountPrefix()
        {
            return Factory::NormalizeAccountPrefix(
                sConfigMgr->GetStringDefault("WorldBots.AccountPrefix", "WBOT", true));
        }

        // Persistence Setting
        static bool ShouldSaveBotProgress()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.SaveBotProgress", true, true);
        }

        static uint32_t GetPersistentBotPercent()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.PersistentBotPercent", 100, true);
            if (configured < 0)
                return 0;
            return configured > 100 ? 100 : static_cast<uint32_t>(configured);
        }

        static uint32_t GetStarterBagItemId()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.StarterBagItemId", 41599, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 0;
        }

        static uint32_t GetStarterBagCount()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.StarterBagCount", 4, true);
            if (configured <= 0)
                return 0;
            return static_cast<uint32_t>(std::min<int32_t>(configured, 4));
        }

        static bool IsGrindFallbackEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.GrindFallback.Enable", true, true);
        }

        static int32_t GetQuestMaxLevelsAboveBot()
        {
            return std::max<int32_t>(0,
                sConfigMgr->GetIntDefault("WorldBots.QuestMaxLevelsAboveBot", 1, true));
        }

        static int32_t GetGrindMinLevelOffset()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.GrindMinLevelOffset", -3, true);
            return std::clamp<int32_t>(configured, -10, 0);
        }

        static int32_t GetGrindMaxLevelOffset()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.GrindMaxLevelOffset", 0, true);
            return std::clamp<int32_t>(configured, -5, 3);
        }

        static uint32_t GetFactoryOperationsPerTick()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.FactoryOpsPerTick", 1, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 1;
        }

        static uint32_t GetMaxConcurrentLogins()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.MaxConcurrentLogins", 8, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 8;
        }

        static uint32_t GetLoginTimeoutMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.LoginTimeoutMs", 120000, true);
            return configured >= 10000 ? static_cast<uint32_t>(configured) : 10000;
        }

        static uint32_t GetLoginMaxRetries()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.LoginMaxRetries", 5, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 0;
        }

        static uint32_t GetLoginRetryInitialDelayMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.LoginRetryInitialDelayMs", 5000, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 1000;
        }

        static uint32_t GetLoginRetryMaxDelayMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.LoginRetryMaxDelayMs", 60000, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 60000;
        }

        static uint32_t GetSaveBatchSize()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.SaveBatchSize", 10, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 1;
        }

        static uint32_t GetSaveBatchIntervalMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.SaveBatchIntervalMs", 500, true);
            return configured >= 100 ? static_cast<uint32_t>(configured) : 100;
        }

        static uint32_t GetSaveBotIntervalMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.SaveBotIntervalMs", 30000, true);
            return configured >= 5000 ? static_cast<uint32_t>(configured) : 5000;
        }

        // Staggered Spawn Delay Settings (ms)
        static uint32_t GetBaseSpawnDelayMs()
        {
            return static_cast<uint32_t>(sConfigMgr->GetIntDefault("WorldBots.BaseSpawnDelayMs", 1000, true));
        }

        static uint32_t GetSpawnDelayStepMs()
        {
            return static_cast<uint32_t>(sConfigMgr->GetIntDefault("WorldBots.SpawnDelayStepMs", 250, true));
        }

        // Default Character Options
        static uint8_t GetDefaultRace()
        {
            return static_cast<uint8_t>(sConfigMgr->GetIntDefault("WorldBots.DefaultRace", 1, true)); // 1 = Human
        }

        static uint8_t GetDefaultClass()
        {
            return static_cast<uint8_t>(sConfigMgr->GetIntDefault("WorldBots.DefaultClass", 1, true)); // 1 = Warrior
        }

        static uint8_t GetDefaultGender()
        {
            return static_cast<uint8_t>(sConfigMgr->GetIntDefault("WorldBots.DefaultGender", 0, true)); // 0 = Male
        }

        static Factory::BotDefinition GetDefaultBotDefinition()
        {
            return { GetDefaultRace(), GetDefaultClass(), GetDefaultGender(), Factory::BehaviorProfile::Balanced };
        }

        static std::vector<Factory::BotDefinition> GetBotRoster();
    };
}
