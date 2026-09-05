#pragma once

#include "BaseBotAction.h"
#include "Combat/ClassStrategies/IClassStrategy.h"
#include "Brain/SuppressionRegistry.h"
#include "Helper/CombatProgressWatchdog.h"
#include "Helper/MovementPathPolicy.h"
#include "Helper/RepeatedPathFailurePolicy.h"
#include "Helper/TargetApproachProgressPolicy.h"
#include "Travel/WorldTravel.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Actions
{
    class GrindAction : public BaseBotAction
    {
    public:
        GrindAction(int32_t minLevelOffset, int32_t maxLevelOffset,
            std::unordered_map<uint64_t, uint32_t> suppressedSpawnIds = {},
            std::unordered_map<uint32_t, uint32_t> suppressedCreatureEntries = {},
            std::vector<Brain::DestinationSuppression> suppressedDestinations = {},
            std::vector<Brain::DangerArea> dangerAreas = {});

        const char* GetName() const override { return "GrindAction"; }
        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement,
            const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;
        bool TryUpdateContext(Player* /*bot*/, const Blackboard::BotBlackboard& /*bb*/) override { return true; }
        bool IsWorldTravelInProgress() const override { return _worldTravel.IsActive(); }
        const char* GetWorldTravelModeName() const override { return _worldTravel.GetCurrentModeName(); }
        const char* GetWorldTravelWaitReasonName() const override { return _worldTravel.GetWaitReasonName(); }
        uint32_t GetWorldTravelElapsedMs() const override { return _worldTravel.GetElapsedMs(); }
        uint32_t GetWorldTravelStepElapsedMs() const override { return _worldTravel.GetStepElapsedMs(); }
        uint32_t GetWorldTravelReplanCount() const override { return _worldTravel.GetReplanCount(); }
        uint32_t GetWorldTravelStepIndex() const override { return _worldTravel.GetStepIndex(); }
        uint32_t GetWorldTravelStepCount() const override { return _worldTravel.GetStepCount(); }
        uint32_t GetRelatedNpcEntry() const override { return _targetEntry; }
        ObjectGuid GetRelatedTargetGuid() const override { return _targetGuid; }
        uint32_t GetHuntingDestinationEntry() const { return _huntingDestinationEntry; }
        const std::string& GetCandidateDiagnostics() const { return _candidateDiagnostics; }
        const std::unordered_map<uint64_t, uint32_t>& GetSuppressedSpawnIds() const
        {
            return _suppressedSpawnIds;
        }
        const std::unordered_map<uint32_t, uint32_t>& GetSuppressedCreatureEntries() const
        {
            return _suppressedCreatureEntries;
        }
        const std::vector<Brain::DestinationSuppression>&
            GetSuppressedDestinations() const
        {
            return _suppressedDestinations;
        }

    private:
        bool IsSafeTarget(Player* bot, Creature* creature) const;
        bool IsTargetSuppressed(Creature* creature) const;
        bool HasPotentialAdd(Player* bot, Creature* target,
            const Blackboard::BotBlackboard& blackboard) const;
        bool IsInsideDangerArea(uint32_t mapId, float x, float y) const;
        bool RouteCrossesDangerArea(uint32_t mapId, float fromX, float fromY,
            float toX, float toY) const;
        Creature* SelectTarget(Player* bot, const Blackboard::BotBlackboard& blackboard);
        bool TravelToHuntingGround(Player* bot, MovementManager* movement);
        bool HandleRepeatedLiveTargetPathFailure(Player* bot,
            Creature* target, MovementManager* movement,
            uint64_t pathGenerationBefore);
        bool HandleLiveTargetApproachStall(Player* bot, Creature* target,
            MovementManager* movement, uint64_t pathGenerationBefore,
            uint32_t deltaMs);
        void RecoverFromNoDamageStall(Player* bot, Creature* target,
            MovementManager* movement);
        void RefreshCandidateDiagnostics();

        int32_t _minLevelOffset;
        int32_t _maxLevelOffset;
        ObjectGuid _targetGuid;
        uint32_t _targetEntry = 0;
        uint32_t _huntingDestinationEntry = 0;
        std::unique_ptr<Combat::IClassStrategy> _classStrategy;
        uint32_t _targetSearchCooldownMs = 0;
        uint32_t _destinationRefreshMs = 0;
        uint32_t _unproductiveMs = 0;
        uint32_t _unreachableAnchorCount = 0;
        uint32_t _liveAnchorMissCount = 0;
        uint64_t _huntingDestinationSpawnId = 0;
        float _huntingDestinationX = 0.0f;
        float _huntingDestinationY = 0.0f;
        float _huntingDestinationZ = 0.0f;
        bool _hasHuntingDestination = false;
        bool _areaRelocationAttempted = false;
        Travel::WorldTravel _worldTravel;
        uint32_t _liveCandidatesSeen = 0;
        uint32_t _liveInvalid = 0;
        uint32_t _liveLevelOrRank = 0;
        uint32_t _liveSuppressed = 0;
        uint32_t _liveDangerArea = 0;
        uint32_t _liveUnsafePack = 0;
        uint32_t _liveEligible = 0;
        std::string _anchorSearchDiagnostics = "not_searched";
        std::string _candidateDiagnostics;
        std::unordered_map<uint64_t, uint32_t> _suppressedSpawnIds;
        std::unordered_map<uint32_t, uint32_t> _suppressedCreatureEntries;
        std::vector<Brain::DestinationSuppression> _suppressedDestinations;
        std::unordered_map<uint64_t, uint32_t> _suppressedLiveTargetGuids;
        Helper::RepeatedPathFailurePolicy::Tracker<uint64_t>
            _liveTargetPathFailures;
        Helper::TargetApproachProgressPolicy::Tracker<uint64_t>
            _liveTargetApproachProgress;
        Helper::CombatProgressWatchdog _combatProgressWatchdog;
        Helper::MovementPathPolicy::TravelProgressTracker
            _huntingTravelProgress;
        std::vector<Brain::DangerArea> _dangerAreas;
    };
}
