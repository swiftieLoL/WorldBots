#pragma once

#include "BaseBotAction.h"
#include "Brain/SuppressionRegistry.h"
#include "Helper/RepeatedPathFailurePolicy.h"
#include "Helper/WanderRecoveryPolicy.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Actions
{
    class WanderAction : public BaseBotAction
    {
    public:
        WanderAction(float originX, float originY, float originZ, float radius,
            std::unordered_map<uint32_t, uint32_t> suppressedQuests = {},
            std::vector<Brain::DestinationSuppression>
                suppressedDestinations = {});

        const char* GetName() const override { return "WanderAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;
        const std::vector<Brain::DestinationSuppression>&
            GetSuppressedDestinations() const
        {
            return _suppressedDestinations;
        }
        bool RequiresOriginRecovery() const
        {
            return _requiresOriginRecovery;
        }

    private:
        enum class DestinationKind : uint8_t
        {
            QuestStarter = 1,
            Settlement = 2,
            GrindingArea = 3,
            Exploration = 4
        };

        bool TryWanderMove(Player* bot, MovementManager* movement,
            DestinationKind kind, uint32_t subjectId, uint32_t questId,
            float x, float y, float z, const char* pathSource);
        bool IsDestinationSuppressed(uint32_t mapId, float x, float y) const;
        static uint64_t MakeDestinationKey(DestinationKind kind,
            uint32_t subjectId, float x, float y, float z);

        uint32_t _pauseTimer;
        std::unordered_map<uint32_t, uint32_t> _suppressedQuests;
        std::vector<Brain::DestinationSuppression> _suppressedDestinations;
        Helper::RepeatedPathFailurePolicy::Tracker<uint64_t>
            _destinationPathFailures;
        Helper::WanderRecoveryPolicy _recoveryPolicy;
        bool _requiresOriginRecovery = false;
    };
}
