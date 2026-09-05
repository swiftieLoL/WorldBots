#pragma once

#include <cstdint>
#include <string>

class Player;

namespace Diagnostics
{
    struct StructuredEvent
    {
        std::string event;
        std::string goal;
        std::string action;
        std::uint64_t actionInstance = 0;
        std::uint32_t questId = 0;
        float requestX = 0.0f;
        float requestY = 0.0f;
        float requestZ = 0.0f;
        bool endpointAvailable = false;
        float endpointX = 0.0f;
        float endpointY = 0.0f;
        float endpointZ = 0.0f;
        std::string pathFailure;
        std::uint32_t pathFlags = 0;
        std::uint64_t pathAttemptGeneration = 0;
        std::uint32_t originFailureCount = 0;
        std::uint32_t originDestinationCount = 0;
        bool originRecoveryRequired = false;
        std::uint32_t retryAfterSeconds = 0;
        std::string outcome;
        std::string failureCategory;
        std::string recoveryDirective;
        std::string details;
    };

    // A focused, append-only event stream for causal debugging. This is
    // separate from TrinityCore's console logger and from the minute-level
    // progress snapshots, so one terminal outcome is written exactly once.
    class StructuredEventLog
    {
    public:
        static void Configure(bool enabled, std::string directory,
            std::string botNames);
        static void Reset();
        static bool IsEnabled();
        static bool ShouldCapture(const Player* bot);
        static void Write(const Player* bot, StructuredEvent event);
        static std::string GetPath();
    };
}
