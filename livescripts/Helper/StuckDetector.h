#pragma once

#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Brain/BotGoal.h"
#include "Blackboard/BotBlackboard.h"
#include "Helper/MovementManager.h"
#include "Helper/EvasionUtils.h"
#include <unordered_map>
#include <cstdint>

namespace Helper
{
    class StuckDetector
    {
    public:
        StuckDetector();

        bool Update(Player* bot, MovementManager* movement, Brain::BotGoal goal,
                    Blackboard::BotBlackboard& blackboard,
                    uint32_t activeQuestId,
                    std::unordered_map<uint32_t, uint32_t>& blacklistedQuests,
                    std::unordered_map<uint32_t, uint32_t>& blacklistedNpcs,
                    uint32_t deltaMs);

        void Reset();

    private:
        float _lastX = 0.0f;
        float _lastY = 0.0f;
        float _lastZ = 0.0f;
        uint32_t _stuckTimerMs = 0;
        uint32_t _sampleTimerMs = 0;
        uint8_t _stuckCount = 0;

        float _stuckZoneX = 0.0f;
        float _stuckZoneY = 0.0f;
        float _stuckZoneZ = 0.0f;
        uint32_t _stuckZoneTimestampSec = 0;
        uint8_t _sameZoneStuckCount = 0;
    };
}
