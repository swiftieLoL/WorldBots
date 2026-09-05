#pragma once

#include "Config.h"
#include "Factory/BotRoster.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace Config
{
    struct BotConfig
    {
        static bool LoadModuleRuntimeConfig(bool forceReload = false);

        static bool IsEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.Enable", true, true);
        }

        // Death Knights are intentionally opt-in while the Scarlet Enclave
        // progression path is under investigation. Keep this gate shared by
        // roster creation, fresh-character creation, and runtime admission so
        // an existing managed DK cannot bypass the setting.
        static bool AreDeathKnightsEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.EnableDeathKnights", false, true);
        }

        static bool IsBotClassAllowed(uint8_t playerClass)
        {
            constexpr uint8_t DeathKnightClassId = 6;
            return playerClass != DeathKnightClassId || AreDeathKnightsEnabled();
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
            return sConfigMgr->GetBoolDefault("WorldBots.Logging.Positions",
                sConfigMgr->GetBoolDefault("WorldBots.DebugMode", false, true), true);
        }

        static bool IsProgressDiagnosticsEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.Diagnostics.Enable", false, true);
        }

        static std::string GetProgressDiagnosticsDirectory()
        {
            return sConfigMgr->GetStringDefault(
                "WorldBots.Diagnostics.Directory", "worldbots-diagnostics", true);
        }

        static uint32_t GetProgressDiagnosticsIntervalMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault(
                "WorldBots.Diagnostics.IntervalSeconds", 60, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 10, 3600)) * 1000;
        }

        static uint32_t GetProgressDiagnosticsStallSeconds()
        {
            int32_t configured = sConfigMgr->GetIntDefault(
                "WorldBots.Diagnostics.StallMinutes", 30, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 1, 1440)) * 60;
        }

        static bool IsStructuredEventDiagnosticsEnabled()
        {
            return sConfigMgr->GetBoolDefault(
                "WorldBots.Diagnostics.Events.Enable", false, true);
        }

        static std::string GetStructuredEventDiagnosticBots()
        {
            return sConfigMgr->GetStringDefault(
                "WorldBots.Diagnostics.Events.Bots", "", true);
        }

        // Retained for compatibility with existing installations and bindings.
        static bool IsVerboseLoggingEnabled()
        {
            return sConfigMgr->GetBoolDefault("WorldBots.VerboseLogging", false, true);
        }

        static std::string GetLoggingMode()
        {
            std::string mode = sConfigMgr->GetStringDefault("WorldBots.Logging", "", true);
            std::transform(mode.begin(), mode.end(), mode.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (mode == "normal" || mode == "verbose" || mode == "important")
                return mode;
            return IsVerboseLoggingEnabled() ? "verbose" : "important";
        }

        static bool IsFileTraceLoggingEnabled()
        {
            return sConfigMgr->GetBoolDefault(
                "WorldBots.Logging.File.Enable", IsProgressDiagnosticsEnabled(), true);
        }

        static std::string GetFileTraceLoggingLevel()
        {
            std::string level = sConfigMgr->GetStringDefault(
                "WorldBots.Logging.File.Level", "verbose", true);
            std::transform(level.begin(), level.end(), level.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (level == "important" || level == "normal" || level == "verbose")
                return level;
            return "verbose";
        }

        static std::string GetFileTraceLoggingBots()
        {
            return sConfigMgr->GetStringDefault(
                "WorldBots.Logging.File.Bots", GetStructuredEventDiagnosticBots(), true);
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

        static float GetMaxRoutineVendorTravelDistance()
        {
            float configured = sConfigMgr->GetFloatDefault(
                "WorldBots.MaxRoutineVendorTravelDistance", 2000.0f, true);
            return std::clamp(configured, 100.0f, 10000.0f);
        }

        static uint32_t GetFactoryOperationsPerTick()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.FactoryOpsPerTick", 1, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 1;
        }

        static uint32_t GetFactoryTaskBudgetMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.FactoryTaskBudgetMs", 5, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 1, 50));
        }

        static uint32_t GetFactoryStartupGraceMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault(
                "WorldBots.FactoryStartupGraceMs", 15000, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 0, 300000));
        }

        static uint32_t GetRuntimeBotBatchSize()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.RuntimeBotBatchSize", 256, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 1, 2000));
        }

        static uint32_t GetRuntimeTaskBudgetMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.RuntimeTaskBudgetMs", 4, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 1, 50));
        }

        static uint32_t GetMaxConcurrentLogins()
        {
            int32_t configured = sConfigMgr->GetIntDefault("WorldBots.MaxConcurrentLogins", 8, true);
            return configured > 0 ? static_cast<uint32_t>(configured) : 8;
        }

        static uint32_t GetLoginLaunchIntervalMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault(
                "WorldBots.LoginLaunchIntervalMs", 500, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 0, 10000));
        }

        static bool PrioritizePlayerLogins()
        {
            return sConfigMgr->GetBoolDefault(
                "WorldBots.PrioritizePlayerLogins", true, true);
        }

        static uint32_t GetPlayerLoginGraceMs()
        {
            int32_t configured = sConfigMgr->GetIntDefault(
                "WorldBots.PlayerLoginGraceMs", 5000, true);
            return static_cast<uint32_t>(std::clamp<int32_t>(configured, 0, 60000));
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
            int32_t val = sConfigMgr->GetIntDefault("WorldBots.BaseSpawnDelayMs", 1000, true);
            return static_cast<uint32_t>(std::clamp(val, 0, 60000));
        }

        static uint32_t GetSpawnDelayStepMs()
        {
            int32_t val = sConfigMgr->GetIntDefault("WorldBots.SpawnDelayStepMs", 250, true);
            return static_cast<uint32_t>(std::clamp(val, 0, 10000));
        }

        // Default Character Options
        static uint8_t GetDefaultRace()
        {
            int32_t val = sConfigMgr->GetIntDefault("WorldBots.DefaultRace", 1, true); // 1 = Human
            return (val >= 1 && val <= 11) ? static_cast<uint8_t>(val) : 1;
        }

        static uint8_t GetDefaultClass()
        {
            int32_t val = sConfigMgr->GetIntDefault("WorldBots.DefaultClass", 1, true); // 1 = Warrior
            return (val >= 1 && val <= 11) ? static_cast<uint8_t>(val) : 1;
        }

        static uint8_t GetDefaultGender()
        {
            int32_t val = sConfigMgr->GetIntDefault("WorldBots.DefaultGender", 0, true); // 0 = Male
            return (val == 0 || val == 1) ? static_cast<uint8_t>(val) : 0;
        }

        static Factory::BotDefinition GetDefaultBotDefinition()
        {
            uint8_t playerClass = GetDefaultClass();
            if (!IsBotClassAllowed(playerClass))
                playerClass = 1; // Warrior: safe non-DK fallback
            return { GetDefaultRace(), playerClass, GetDefaultGender(), Factory::BehaviorProfile::Balanced };
        }

        static std::vector<Factory::BotDefinition> GetBotRoster();
    };
}
