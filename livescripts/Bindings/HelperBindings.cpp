#include "Globals/ObjectMgr.h"
#include "HelperBindings.h"
#include "Core/CoreLogic.h"
#include <string_view>

void BotFactorySpawn(uint32_t botCount, bool debugMode, bool verboseLogging)
{
    Core::CoreLogic::InitializeBotFactory(botCount, debugMode, verboseLogging);
}

bool BotFactorySpawnConfigured()
{
    return Core::CoreLogic::InitializeFromConfig();
}

void BotFactoryUpdate(uint32_t diff)
{
    Core::CoreLogic::Update(diff);
}

uint32_t BotFactoryGetActiveCount()
{
    return Core::CoreLogic::GetActiveBotCount();
}

void BotFactorySetVerboseLogging(bool enabled)
{
    Core::CoreLogic::SetVerboseLogging(enabled);
}

std::string BotHandleCommand(std::string const& command)
{
    std::string_view input = command;

    while (!input.empty() && (input.front() == ' ' || input.front() == '.'))
        input.remove_prefix(1);
    while (!input.empty() && input.back() == ' ')
        input.remove_suffix(1);

    constexpr std::string_view testPrefix = "bot test";
    if (input.size() >= testPrefix.size() && input.substr(0, testPrefix.size()) == testPrefix &&
        (input.size() == testPrefix.size() || input[testPrefix.size()] == ' '))
    {
        input.remove_prefix(testPrefix.size());
        return Core::CoreLogic::RunTestCommand(std::string(input));
    }

    constexpr std::string_view tracePrefix = "bot trace";
    if (input.size() >= tracePrefix.size() && input.substr(0, tracePrefix.size()) == tracePrefix &&
        (input.size() == tracePrefix.size() || input[tracePrefix.size()] == ' '))
    {
        input.remove_prefix(tracePrefix.size());
        return Core::CoreLogic::RunTraceCommand(std::string(input));
    }

    constexpr std::string_view statusPrefix = "bot status";
    if (input == "bot")
        input = statusPrefix;

    if (input.size() < statusPrefix.size() || input.substr(0, statusPrefix.size()) != statusPrefix)
        return {};
    if (input.size() > statusPrefix.size() && input[statusPrefix.size()] != ' ')
        return {};

    input.remove_prefix(statusPrefix.size());
    while (!input.empty() && input.front() == ' ')
        input.remove_prefix(1);

    constexpr std::string_view vendorMode = "vendor";
    if (input.size() >= vendorMode.size() && input.substr(0, vendorMode.size()) == vendorMode &&
        (input.size() == vendorMode.size() || input[vendorMode.size()] == ' '))
    {
        input.remove_prefix(vendorMode.size());
        while (!input.empty() && input.front() == ' ')
            input.remove_prefix(1);
        return Core::CoreLogic::GetBotVendorStatus(input.empty() ? "Botharry" : std::string(input));
    }

    constexpr std::string_view factoryMode = "factory";
    if (input == factoryMode || input == "factory status")
        return Core::CoreLogic::GetFactoryStatus();

    constexpr std::string_view questMode = "quest";
    if (input.size() >= questMode.size() && input.substr(0, questMode.size()) == questMode &&
        (input.size() == questMode.size() || input[questMode.size()] == ' '))
    {
        input.remove_prefix(questMode.size());
        while (!input.empty() && input.front() == ' ')
            input.remove_prefix(1);
        return Core::CoreLogic::GetBotQuestStatus(input.empty() ? "Botharry" : std::string(input));
    }

    std::string botName = input.empty() ? "Botharry" : std::string(input);
    return Core::CoreLogic::GetBotStatus(botName);
}

bool BotMoveTo(uint32_t botGuidLow, float x, float y, float z)
{
    return Core::CoreLogic::BotMoveTo(botGuidLow, x, y, z);
}

void BotFollow(uint32_t botGuidLow, uint32_t targetGuidLow, float distance, float angle)
{
    Core::CoreLogic::BotFollow(botGuidLow, targetGuidLow, distance, angle);
}

void BotChase(uint32_t botGuidLow, uint32_t targetGuidLow)
{
    Core::CoreLogic::BotChase(botGuidLow, targetGuidLow);
}

void BotStop(uint32_t botGuidLow)
{
    Core::CoreLogic::BotStop(botGuidLow);
}

uint8_t BotGetMovementState(uint32_t botGuidLow)
{
    return Core::CoreLogic::BotGetMovementState(botGuidLow);
}
