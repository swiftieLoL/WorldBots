#include "HelperBindings.h"
#include "Core/CoreLogic.h"
#include "Commands/BotCommands.h"

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
    return Commands::BotCommands::Handle(command);
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
