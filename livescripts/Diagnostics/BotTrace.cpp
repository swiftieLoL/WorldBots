#include "BotTrace.h"
#include "DiagnosticsUtils.h"

#include "Log.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>

namespace Diagnostics
{
    namespace
    {
        std::atomic<LogMode> s_mode{ LogMode::Important };
        std::unordered_set<uint32_t> s_tracedBotGuids;
        std::mutex s_tracedMutex;

        bool s_fileEnabled = false;
        LogMode s_fileMode = LogMode::Verbose;
        std::ofstream s_fileStream;
        std::recursive_mutex s_fileMutex;
        std::string s_filePath;
        std::unordered_set<std::string> s_fileBotNames;
        bool s_fileCaptureAll = true;

        constexpr bool ShouldEmit(LogMode mode, LogEvent event, bool traced)
        {
            return event == LogEvent::Important || mode == LogMode::Verbose || traced ||
                (event == LogEvent::Normal && mode == LogMode::Normal);
        }

        static_assert(ShouldEmit(LogMode::Important, LogEvent::Important, false));
        static_assert(!ShouldEmit(LogMode::Important, LogEvent::Normal, false));
        static_assert(ShouldEmit(LogMode::Important, LogEvent::Detail, true));
        static_assert(ShouldEmit(LogMode::Normal, LogEvent::Normal, false));
        static_assert(!ShouldEmit(LogMode::Normal, LogEvent::Detail, false));
        static_assert(ShouldEmit(LogMode::Verbose, LogEvent::Detail, false));
    }

    void BotTrace::SetMode(LogMode mode)
    {
        s_mode = mode;
    }

    LogMode BotTrace::GetMode()
    {
        return s_mode;
    }

    const char* BotTrace::GetModeName()
    {
        switch (s_mode)
        {
            case LogMode::Normal: return "normal";
            case LogMode::Verbose: return "verbose";
            default: return "important";
        }
    }

    void BotTrace::SetGlobalVerbose(bool enabled)
    {
        s_mode = enabled ? LogMode::Verbose : LogMode::Important;
    }

    bool BotTrace::IsGlobalVerbose()
    {
        return s_mode == LogMode::Verbose;
    }

    void BotTrace::SetEnabled(uint32_t guidLow, bool enabled)
    {
        if (guidLow == 0)
            return;

        std::lock_guard<std::mutex> lock(s_tracedMutex);
        if (enabled)
            s_tracedBotGuids.insert(guidLow);
        else
            s_tracedBotGuids.erase(guidLow);
    }

    bool BotTrace::IsEnabled(uint32_t guidLow)
    {
        if (guidLow == 0)
            return false;
        std::lock_guard<std::mutex> lock(s_tracedMutex);
        return s_tracedBotGuids.find(guidLow) != s_tracedBotGuids.end();
    }

    bool BotTrace::ShouldLog(const Player* bot, LogEvent event)
    {
        uint32_t guidLow = bot
            ? static_cast<uint32_t>(bot->GetGUID().GetCounter()) : 0;
        return ShouldLogGuid(guidLow, event);
    }

    bool BotTrace::ShouldLogGuid(uint32_t guidLow, LogEvent event)
    {
        return ShouldEmit(s_mode.load(std::memory_order_relaxed), event, IsEnabled(guidLow));
    }

    void BotTrace::Clear()
    {
        std::lock_guard<std::mutex> lock(s_tracedMutex);
        s_tracedBotGuids.clear();
    }

    void BotTrace::ConfigureFileLog(bool enabled, const std::string& directory,
        LogMode fileMode, const std::string& botFilter)
    {
        std::lock_guard<std::recursive_mutex> lock(s_fileMutex);
        CloseFileLog();

        s_fileEnabled = enabled;
        s_fileMode = fileMode;
        s_fileBotNames.clear();
        s_fileCaptureAll = true;

        if (!enabled)
            return;

        std::filesystem::path base(directory.empty() ? "worldbots-diagnostics" : directory);
        s_filePath = (base / "worldbots-trace.log").string();
        if (!EnsureParentDirectory(s_filePath))
        {
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not create trace log directory '{}'; file trace logging disabled.", directory);
            s_fileEnabled = false;
            return;
        }

        std::string normalizedFilter = NormalizeName(botFilter);
        if (!normalizedFilter.empty() && normalizedFilter != "all")
        {
            s_fileCaptureAll = false;
            std::size_t start = 0;
            while (start < botFilter.size())
            {
                std::size_t comma = botFilter.find(',', start);
                std::size_t end = (comma == std::string::npos) ? botFilter.size() : comma;
                std::string token = NormalizeName(botFilter.substr(start, end - start));
                if (!token.empty())
                    s_fileBotNames.insert(token);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }

        s_fileStream.open(s_filePath, std::ios::out | std::ios::app);
        if (s_fileStream.is_open())
        {
            s_fileStream << "# WorldBots File Trace Logger Started at "
                << FormatTimestampIso() << "\n";
            s_fileStream.flush();
            TC_LOG_INFO("server", "[WorldBots] [Diagnostics] Verbose file trace logging enabled: '{}' (level: {})",
                s_filePath, fileMode == LogMode::Verbose ? "verbose" : (fileMode == LogMode::Normal ? "normal" : "important"));
        }
        else
        {
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not open trace log file '{}'.", s_filePath);
            s_fileEnabled = false;
        }
    }

    void BotTrace::CloseFileLog()
    {
        std::lock_guard<std::recursive_mutex> lock(s_fileMutex);
        if (s_fileStream.is_open())
        {
            s_fileStream << "# WorldBots File Trace Logger Stopped at "
                << FormatTimestampIso() << "\n";
            s_fileStream.flush();
            s_fileStream.close();
        }
        s_fileEnabled = false;
        s_filePath.clear();
    }

    bool BotTrace::IsFileLogEnabled()
    {
        return s_fileEnabled;
    }

    void BotTrace::LogToFile(const Player* bot, const char* category,
        const std::string& message, LogEvent severity)
    {
        if (!s_fileEnabled || !bot)
            return;

        uint32_t guidLow = static_cast<uint32_t>(bot->GetGUID().GetCounter());
        bool isExplicitlyTraced = IsEnabled(guidLow);
        if (!ShouldEmit(s_fileMode, severity, isExplicitlyTraced))
            return;

        std::lock_guard<std::recursive_mutex> lock(s_fileMutex);
        if (!s_fileCaptureAll)
        {
            if (s_fileBotNames.find(NormalizeName(bot->GetName())) == s_fileBotNames.end())
                return;
        }

        if (!s_fileStream.is_open())
            return;

        s_fileStream << "[" << FormatTimestampIso() << "] "
            << "[" << bot->GetName() << " (Guid:" << guidLow
            << ", L" << static_cast<uint32_t>(bot->GetLevel()) << ")] "
            << "[" << (category ? category : "General") << "] "
            << message << "\n";
        s_fileStream.flush();
    }
}

