#pragma once

#include <cstdint>
#include <string>

class Player;

namespace Diagnostics
{
    enum class LogMode : uint8_t
    {
        Important,
        Normal,
        Verbose
    };

    enum class LogEvent : uint8_t
    {
        Important,
        Normal,
        Detail
    };

    class BotTrace
    {
    public:
        // TrinityCore severity and WorldBots event importance are separate.
        // Every routine bot-specific INFO/WARN/ERROR site must consult this
        // policy so warning severity cannot bypass focused logging.
        static void SetMode(LogMode mode);
        static LogMode GetMode();
        static const char* GetModeName();

        // Compatibility surface for the existing TypeScript binding.
        static void SetGlobalVerbose(bool enabled);
        static bool IsGlobalVerbose();

        static void SetEnabled(uint32_t guidLow, bool enabled);
        static bool IsEnabled(uint32_t guidLow);
        static bool ShouldLog(const Player* bot, LogEvent event = LogEvent::Detail);
        static bool ShouldLogGuid(uint32_t guidLow, LogEvent event = LogEvent::Detail);
        static void Clear();

        // File-backed trace logging outside the console
        static void ConfigureFileLog(bool enabled, const std::string& directory,
            LogMode fileMode = LogMode::Verbose, const std::string& botFilter = "");
        static void CloseFileLog();
        static bool IsFileLogEnabled();
        static void LogToFile(const Player* bot, const char* category,
            const std::string& message, LogEvent severity = LogEvent::Detail);
    };
}
