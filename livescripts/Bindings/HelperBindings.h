#pragma once

#include <cstdint>
#include <string>

void BotFactorySpawn(uint32_t botCount, bool debugMode, bool verboseLogging = false);
bool BotFactorySpawnConfigured();
void BotFactoryUpdate(uint32_t diff);
uint32_t BotFactoryGetActiveCount();
void BotFactorySetVerboseLogging(bool enabled);
std::string BotHandleCommand(std::string const& command);

bool BotMoveTo(uint32_t botGuidLow, float x, float y, float z);
void BotFollow(uint32_t botGuidLow, uint32_t targetGuidLow, float distance = 2.0f, float angle = 0.0f);
void BotChase(uint32_t botGuidLow, uint32_t targetGuidLow);
void BotStop(uint32_t botGuidLow);
uint8_t BotGetMovementState(uint32_t botGuidLow);
