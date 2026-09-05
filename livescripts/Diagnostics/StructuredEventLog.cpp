#include "StructuredEventLog.h"
#include "DiagnosticsUtils.h"

#include "Log.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace Diagnostics
{
    namespace
    {
        constexpr const char* Header =
            "unix_ms\tsession_ms\tevent_seq\tguid\tname\tlevel\tmap\tzone\tarea\tx\ty\tz"
            "\tevent\tgoal\taction\taction_instance\tquest_id"
            "\trequest_x\trequest_y\trequest_z\tendpoint_available"
            "\tendpoint_x\tendpoint_y\tendpoint_z\tpath_failure\tpath_flags"
            "\tpath_attempt_gen\torigin_path_failures\torigin_path_destinations"
            "\torigin_recovery_required\tretry_after_s\toutcome\tfailure_category"
            "\trecovery_directive\tdetails\n";

        bool s_enabled = false;
        bool s_captureAll = false;
        bool s_writeErrorReported = false;
        std::uint64_t s_sessionStartedMs = 0;
        std::uint64_t s_eventSequence = 0;
        std::string s_path;
        std::ofstream s_stream;
        std::unordered_set<std::string> s_botNames;
        std::mutex s_mutex;

        inline CleanFieldWriter Clean(std::string_view value)
        {
            return AsCleanField(value);
        }

        bool CapturesUnlocked(const Player* bot)
        {
            if (!s_enabled || !bot)
                return false;
            if (s_captureAll)
                return true;
            return s_botNames.find(NormalizeName(bot->GetName())) != s_botNames.end();
        }
    }

    void StructuredEventLog::Configure(bool enabled, std::string directory,
        std::string botNames)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_enabled = false;
        s_captureAll = false;
        s_writeErrorReported = false;
        s_sessionStartedMs = 0;
        s_eventSequence = 0;
        if (s_stream.is_open())
            s_stream.close();
        s_stream.clear();
        s_path.clear();
        s_botNames.clear();

        if (!enabled)
            return;

        std::istringstream names(botNames);
        std::string name;
        while (std::getline(names, name, ','))
        {
            name = NormalizeName(std::move(name));
            if (name.empty())
                continue;
            if (name == "*" || name == "all")
                s_captureAll = true;
            else
                s_botNames.insert(std::move(name));
        }
        if (!s_captureAll && s_botNames.empty())
            return;

        std::filesystem::path base(directory.empty()
            ? "worldbots-diagnostics" : directory);
        s_path = (base / "worldbots-events-v1.tsv").string();
        std::error_code error;
        std::filesystem::create_directories(base, error);
        if (error)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not create structured-event directory '{}': {}",
                base.string(), error.message());
            s_path.clear();
            return;
        }

        bool writeHeader = IsEmptyOrMissing(s_path);
        s_stream.open(s_path, std::ios::out | std::ios::app | std::ios::binary);
        if (!s_stream)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not open structured event file '{}'.",
                s_path);
            s_stream.clear();
            s_path.clear();
            return;
        }
        if (writeHeader)
        {
            s_stream << Header;
            s_stream.flush();
        }
        s_sessionStartedMs = UnixMilliseconds();
        s_enabled = true;
        TC_LOG_INFO("server", "[WorldBots] [Diagnostics] Focused structured events enabled: file='{}', bots='{}'.",
            s_path, s_captureAll ? "all" : botNames);
    }

    void StructuredEventLog::Reset()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_enabled = false;
        s_captureAll = false;
        s_writeErrorReported = false;
        s_sessionStartedMs = 0;
        s_eventSequence = 0;
        if (s_stream.is_open())
        {
            s_stream.flush();
            s_stream.close();
        }
        s_stream.clear();
        s_path.clear();
        s_botNames.clear();
    }

    bool StructuredEventLog::IsEnabled()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_enabled;
    }

    bool StructuredEventLog::ShouldCapture(const Player* bot)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return CapturesUnlocked(bot);
    }

    void StructuredEventLog::Write(const Player* bot, StructuredEvent event)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!CapturesUnlocked(bot))
            return;

        if (!s_stream.is_open())
        {
            if (!s_writeErrorReported)
            {
                s_writeErrorReported = true;
                TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not append structured event file '{}'.",
                    s_path);
            }
            return;
        }

        ++s_eventSequence;
        s_stream << std::fixed << std::setprecision(1)
            << UnixMilliseconds() << '\t' << s_sessionStartedMs << '\t'
            << s_eventSequence << '\t'
            << bot->GetGUID().GetCounter() << '\t' << Clean(bot->GetName()) << '\t'
            << static_cast<std::uint32_t>(bot->GetLevel()) << '\t'
            << bot->GetMapId() << '\t' << bot->GetZoneId() << '\t' << bot->GetAreaId() << '\t'
            << bot->GetPositionX() << '\t' << bot->GetPositionY() << '\t'
            << bot->GetPositionZ() << '\t' << Clean(std::move(event.event)) << '\t'
            << Clean(std::move(event.goal)) << '\t' << Clean(std::move(event.action)) << '\t'
            << event.actionInstance << '\t' << event.questId << '\t'
            << event.requestX << '\t' << event.requestY << '\t' << event.requestZ << '\t'
            << event.endpointAvailable << '\t' << event.endpointX << '\t'
            << event.endpointY << '\t' << event.endpointZ << '\t'
            << Clean(std::move(event.pathFailure)) << '\t' << event.pathFlags << '\t'
            << event.pathAttemptGeneration << '\t' << event.originFailureCount << '\t'
            << event.originDestinationCount << '\t' << event.originRecoveryRequired << '\t'
            << event.retryAfterSeconds << '\t' << Clean(std::move(event.outcome)) << '\t'
            << Clean(std::move(event.failureCategory)) << '\t'
            << Clean(std::move(event.recoveryDirective)) << '\t'
            << Clean(std::move(event.details)) << '\n';
        // Preserve the previous per-event durability while avoiding repeated
        // open/close and path resolution on every diagnostic event.
        s_stream.flush();
        if (!s_stream && !s_writeErrorReported)
        {
            s_writeErrorReported = true;
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not append structured event file '{}'.",
                s_path);
        }
    }

    std::string StructuredEventLog::GetPath()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_path;
    }
}
