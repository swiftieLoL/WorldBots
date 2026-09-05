#pragma once

#include "Blackboard/BotBlackboard.h"
#include <cstdint>
#include <limits>
#include <utility>

class MovementManager;
class Player;

namespace Sense
{
    namespace Detail
    {
        template <typename StateStruct, typename UpdateFn>
        bool ServiceSubstate(StateStruct& state, uint32_t deltaMs, UpdateFn&& updateFn)
        {
            state.elapsedMs = deltaMs > std::numeric_limits<uint32_t>::max() - state.elapsedMs
                ? std::numeric_limits<uint32_t>::max() : state.elapsedMs + deltaMs;
            state.ageMs = !state.initialized || deltaMs > std::numeric_limits<uint32_t>::max() - state.ageMs
                ? std::numeric_limits<uint32_t>::max() : state.ageMs + deltaMs;
            if (state.refreshIntervalMs == 0 || state.elapsedMs < state.refreshIntervalMs)
                return false;

            state.elapsedMs %= state.refreshIntervalMs;
            std::forward<UpdateFn>(updateFn)();
            state.initialized = true;
            state.ageMs = 0;
            return true;
        }
    }

    class ISenseUpdater
    {
    public:
        virtual ~ISenseUpdater() = default;
        virtual bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) = 0;
    };

    class SelfSenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
    private:
        static void Refresh(Player*, Blackboard::SelfState&);
    };

    class SpatialSenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
    private:
        static void Refresh(Player*, Blackboard::SpatialState&);
    };

    class PartySenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
        static void ClearSharedCache();
    private:
        static void Refresh(Player*, Blackboard::PartyState&);
    };

    class CombatSenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
    private:
        static void Refresh(Player*, Blackboard::CombatState&);
    };

    class NavigationSenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
    private:
        static void Refresh(Player*, MovementManager*, Blackboard::NavigationState&);
    };

    class InventorySenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
    private:
        static void Refresh(Player*, Blackboard::InventoryState&);
    };

    class QuestSenseUpdater final : public ISenseUpdater
    {
    public:
        bool Update(Player*, MovementManager*, Blackboard::BotBlackboard&, uint32_t) override;
    private:
        static void Refresh(Player*, Blackboard::QuestState&);
        static void EvaluateQuestLog(Player*, Blackboard::QuestState&);
        static void ScanNearbyQuestGivers(Player*, Blackboard::QuestState&);
        static void LocateWorldStarter(Player*, Blackboard::QuestState&);
    };
}
