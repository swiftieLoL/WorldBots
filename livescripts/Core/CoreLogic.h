#pragma once

#include <cstdint>
#include <string>

namespace Core
{
    class CoreLogic
    {
    public:
        static void InitializeBotFactory(uint32_t botCount, bool debugMode, bool verboseLogging);
        static bool InitializeFromConfig();
        static void Update(uint32_t diff);

        static uint32_t GetActiveBotCount();
        static std::string GetFactoryStatus();
        static std::string GetBotStatus(std::string const& botName);
        static std::string GetBotVendorStatus(std::string const& botName);
        static std::string GetBotQuestStatus(std::string const& botName);
        static std::string RunTestCommand(std::string const& arguments);
        static std::string RunTraceCommand(std::string const& arguments);

        static void SetVerboseLogging(bool enabled);
        static bool IsVerboseLoggingEnabled();

        // High-level bot movement API wrappers
        static bool BotMoveTo(uint32_t botGuidLow, float x, float y, float z);
        static void BotFollow(uint32_t botGuidLow, uint32_t targetGuidLow, float distance, float angle);
        static void BotChase(uint32_t botGuidLow, uint32_t targetGuidLow);
        static void BotStop(uint32_t botGuidLow);
        static uint8_t BotGetMovementState(uint32_t botGuidLow);
    };
}
