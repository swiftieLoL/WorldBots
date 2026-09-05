#pragma once

#include "Actions/ActionTypes.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Brain
{
    enum class QuestFailureKind : uint8_t
    {
        Transient,
        Navigation,
        UnsafeRoute,
        Interaction,
        Stalled,
        ProgressionDifficulty,
        ContentUnsupported
    };

    struct QuestFailureDecision
    {
        uint32_t failureCount = 0;
        uint32_t retryAtLevel = 0;
        bool sessionBlocked = false;
        bool newlyEscalated = false;
    };

    // Remembers failures that a short timed blacklist cannot solve. Recoverable
    // failures are retried after a different quest when possible, then deferred
    // until the next level if they repeat. Content the bot cannot automate is
    // excluded for the lifetime of this brain instead of being retried hourly.
    class QuestFailureMemory
    {
    public:
        static constexpr uint32_t RecoverableFailureThreshold = 2;
        static constexpr uint32_t InteractionFailureThreshold = 3;

        QuestFailureDecision RecordFailure(uint32_t questId, uint32_t botLevel,
            QuestFailureKind kind)
        {
            if (questId == 0 || kind == QuestFailureKind::Transient)
                return {};

            State& state = _states[questId];
            QuestFailureDecision decision;
            if (kind == QuestFailureKind::ContentUnsupported)
            {
                decision.newlyEscalated = !state.sessionBlocked;
                state.sessionBlocked = true;
                ++state.failureCount;
                decision.failureCount = state.failureCount;
                decision.sessionBlocked = true;
                return decision;
            }

            if (state.failureLevel != botLevel)
            {
                state.failureLevel = botLevel;
                state.failureCount = 0;
                state.retryAtLevel = 0;
            }

            ++state.failureCount;
            uint32_t threshold = kind == QuestFailureKind::ProgressionDifficulty
                ? 1u
                : (kind == QuestFailureKind::Interaction
                    ? InteractionFailureThreshold : RecoverableFailureThreshold);
            if (state.failureCount >= threshold && state.retryAtLevel <= botLevel)
            {
                state.retryAtLevel = botLevel < std::numeric_limits<uint8_t>::max()
                    ? botLevel + 1 : botLevel;
                decision.newlyEscalated = true;
            }

            decision.failureCount = state.failureCount;
            decision.retryAtLevel = state.retryAtLevel;
            decision.sessionBlocked = state.sessionBlocked;
            return decision;
        }

        void RecordSuccess(uint32_t questId)
        {
            if (questId != 0)
                _states.erase(questId);
        }

        void PruneOutleveled(uint32_t botLevel, uint32_t maxLevelDelta = 7)
        {
            for (auto it = _states.begin(); it != _states.end(); )
            {
                if (!it->second.sessionBlocked && botLevel > it->second.failureLevel + maxLevelDelta)
                    it = _states.erase(it);
                else
                    ++it;
            }
        }

        bool IsDeferred(uint32_t questId, uint32_t botLevel) const
        {
            auto it = _states.find(questId);
            return it != _states.end() &&
                (it->second.sessionBlocked || it->second.retryAtLevel > botLevel);
        }

        bool IsSessionBlocked(uint32_t questId) const
        {
            auto it = _states.find(questId);
            return it != _states.end() && it->second.sessionBlocked;
        }

        uint32_t GetRetryLevel(uint32_t questId) const
        {
            auto it = _states.find(questId);
            return it == _states.end() ? 0 : it->second.retryAtLevel;
        }

        uint32_t GetFailureCount(uint32_t questId, uint32_t botLevel) const
        {
            auto it = _states.find(questId);
            if (it == _states.end() || it->second.failureLevel != botLevel)
                return 0;
            return it->second.failureCount;
        }

        uint32_t GetNextRetryLevel(uint32_t botLevel) const
        {
            uint32_t next = 0;
            for (const auto& [questId, state] : _states)
            {
                (void)questId;
                if (state.retryAtLevel <= botLevel)
                    continue;
                next = next == 0 ? state.retryAtLevel
                    : std::min(next, state.retryAtLevel);
            }
            return next;
        }

        std::unordered_set<uint32_t> GetDeferredQuestIds(uint32_t botLevel) const
        {
            std::unordered_set<uint32_t> result;
            for (const auto& [questId, state] : _states)
            {
                if (state.sessionBlocked || state.retryAtLevel > botLevel)
                    result.insert(questId);
            }
            return result;
        }

    private:
        struct State
        {
            uint32_t failureLevel = 0;
            uint32_t failureCount = 0;
            uint32_t retryAtLevel = 0;
            bool sessionBlocked = false;
        };

        std::unordered_map<uint32_t, State> _states;
    };

    inline QuestFailureKind ClassifyQuestFailure(
        Actions::ActionOutcome outcome, Actions::FailureCategory category)
    {
        if (outcome == Actions::ActionOutcome::Unsupported ||
            category == Actions::FailureCategory::ContentUnsupported)
            return QuestFailureKind::ContentUnsupported;
        if (category == Actions::FailureCategory::ProgressionDifficulty)
            return QuestFailureKind::ProgressionDifficulty;
        if (category == Actions::FailureCategory::Stalled)
            return QuestFailureKind::Stalled;
        if (category == Actions::FailureCategory::Navigation)
            return QuestFailureKind::Navigation;
        if (category == Actions::FailureCategory::Interaction ||
            category == Actions::FailureCategory::ServiceCapability)
            return QuestFailureKind::Interaction;
        return QuestFailureKind::Transient;
    }
}
