#include "ProgressMonitor.h"
#include "DiagnosticsUtils.h"

#include "Log.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Diagnostics
{
    std::unordered_map<uint64_t, ProgressMonitor::ProgressState> ProgressMonitor::s_progress;
    std::mutex ProgressMonitor::s_mutex;
    bool ProgressMonitor::s_enabled = false;
    uint32_t ProgressMonitor::s_stallSeconds = 1800;
    std::string ProgressMonitor::s_latestPath;
    std::string ProgressMonitor::s_historyPath;

    namespace
    {
        constexpr const char* Header =
            "unix_ms\tguid\tname\truntime_active\tlevel\txp\tnext_level_xp\tprogress_age_s\tstalled\tsuspected_reason"
            "\tprofile\tgoal\ttier\taction\toutcome\taction_detail\taction_age_s\taction_idle_s"
            "\ttravel_mode\ttravel_wait\tcasting\ttravel_age_s\ttravel_step_s\ttravel_replans\ttravel_step\ttravel_steps"
            "\tactive_quest\twatchdog_quest\twatchdog_s"
            "\tdeath_recovery_s\tmap\tzone\tarea\tx\ty\tz\thp_pct\tmana_pct\tdead\tin_combat"
            "\tsnapshot_ready\tmovement\thas_path\tpath_failure\tpath_flags\tpath_attempt_gen\tpath_evidence_fresh"
            "\torigin_path_failures\torigin_path_destinations\torigin_recovery_required"
            "\tpath_request_x\tpath_request_y\tpath_request_z\tpath_endpoint_available"
            "\tpath_endpoint_x\tpath_endpoint_y\tpath_endpoint_z"
            "\texternal_control\tnav_stuck"
            "\tfree_bag_slots\ttotal_bag_slots\tbags_full\tneeds_repair\tneeds_restock"
            "\tavailable_quests\tactive_quests\tcompleted_quests\ttotal_transitions"
            "\trecent_interrupted\tchurning\tdeaths\tcircuit_breaks\tquest_suppress\tnpc_suppress"
            "\tgoal_churns\tinv_deadlocks\tstuck_escalations\taction_bugs\tactions_interrupted\n";

        inline CleanFieldWriter Clean(std::string_view value)
        {
            return AsCleanField(value);
        }
    }

    void ProgressMonitor::Configure(bool enabled, std::string directory, uint32_t stallSeconds)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_progress.clear();
        s_enabled = enabled;
        s_stallSeconds = std::max<uint32_t>(60, stallSeconds);
        if (!s_enabled)
        {
            s_latestPath.clear();
            s_historyPath.clear();
            return;
        }

        std::filesystem::path base(directory.empty() ? "worldbots-diagnostics" : directory);
        s_latestPath = (base / "worldbots-live.tsv").string();
        // Preserve the original history schema. New diagnostic columns write
        // to a versioned stream so old rows never become misaligned with a new
        // header after a realm restart.
        s_historyPath = (base / "worldbots-history-v5.tsv").string();
        if (!EnsureParentDirectory(s_latestPath) || !EnsureParentDirectory(s_historyPath))
        {
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not create diagnostics directory '{}'; file diagnostics are disabled.", directory);
            s_enabled = false;
            return;
        }

        TC_LOG_INFO("server", "[WorldBots] [Diagnostics] Progress snapshots enabled: latest='{}', history='{}', stall threshold={} seconds.",
            s_latestPath, s_historyPath, s_stallSeconds);
    }

    std::string ProgressMonitor::Classify(const ProgressSample& sample, uint64_t progressAgeSeconds)
    {
        if (!sample.runtimeActive) return "not_active_or_login_pending";
        if (!sample.snapshotReady) return "waiting_for_senses";
        if (sample.dead) return "dead_or_resurrecting";
        if (sample.deathRecoverySeconds > 0) return "death_recovery";
        if (sample.externalControl != "None") return "external_control";
        if (sample.originRecoveryRequired) return "origin_off_navmesh_recovery";
        if (sample.navStuck || sample.movement == "Stuck") return "navigation_stuck";
        if (sample.watchdogMs >= 120000) return "quest_no_progress";
        if (sample.churning) return "goal_churn";
        if (sample.actionIdleMs >= 60000) return "action_idle_without_travel";
        if (sample.bagsFull || (sample.freeBagSlots == 0 && sample.totalBagSlots > 0)) return "inventory_full";
        if (sample.goal == "Idle") return "idle";
        if (sample.goal == "Grind" && !sample.inCombat && !sample.hasPath) return "grind_without_target_or_path";
        if (sample.goal == "ProgressQuest" && !sample.inCombat && !sample.hasPath) return "quest_without_target_or_path";
        if (sample.goal == "TownRun" && !sample.hasPath) return "town_run_without_path";
        if (progressAgeSeconds >= s_stallSeconds) return "no_xp_progress_observed";
        return "progressing_or_within_grace";
    }

    void ProgressMonitor::WriteSnapshot(std::vector<ProgressSample> samples)
    {
        std::string latestPath;
        std::string historyPath;
        uint32_t stallSeconds = 1800;

        const uint64_t nowMs = UnixMilliseconds();
        struct Row { ProgressSample sample; uint64_t ageSeconds; std::string reason; };
        std::vector<Row> rows;
        rows.reserve(samples.size());

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (!s_enabled)
                return;

            latestPath = s_latestPath;
            historyPath = s_historyPath;
            stallSeconds = s_stallSeconds;

            for (ProgressSample& sample : samples)
            {
                const uint64_t trackingKey = sample.guidLow != 0
                    ? sample.guidLow
                    : (0x100000000ULL | static_cast<uint64_t>(std::hash<std::string>{}(sample.name)));
                ProgressState& state = s_progress[trackingKey];
                if (state.lastProgressMs == 0 || (sample.runtimeActive &&
                    (state.level != sample.level || state.xp != sample.xp)))
                {
                    state.level = sample.level;
                    state.xp = sample.xp;
                    state.lastProgressMs = nowMs;
                }
                uint64_t ageSeconds = (nowMs - state.lastProgressMs) / 1000;
                rows.push_back({ std::move(sample), ageSeconds, {} });
                rows.back().reason = Classify(rows.back().sample, ageSeconds);
            }
        }

        std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
            if (left.sample.level != right.sample.level)
                return left.sample.level < right.sample.level;
            if (left.ageSeconds != right.ageSeconds)
                return left.ageSeconds > right.ageSeconds;
            return left.sample.name < right.sample.name;
        });

        std::ostringstream body;
        body << Header << std::fixed << std::setprecision(1);
        for (const Row& row : rows)
        {
            const ProgressSample& s = row.sample;
            const bool stalled = row.ageSeconds >= stallSeconds;
            body << nowMs << '\t' << s.guidLow << '\t' << Clean(s.name) << '\t' << s.runtimeActive << '\t'
                 << static_cast<uint32_t>(s.level) << '\t' << s.xp << '\t' << s.nextLevelXp << '\t'
                 << row.ageSeconds << '\t' << (stalled ? 1 : 0) << '\t' << row.reason << '\t'
                 << Clean(s.profile) << '\t' << Clean(s.goal) << '\t' << Clean(s.tier) << '\t'
                 << Clean(s.action) << '\t' << Clean(s.outcome) << '\t'
                 << Clean(s.actionDetail) << '\t'
                 << s.actionElapsedMs / 1000 << '\t' << s.actionIdleMs / 1000 << '\t'
                 << Clean(s.travelMode) << '\t' << Clean(s.travelWaitReason) << '\t'
                 << s.casting << '\t' << s.travelElapsedMs / 1000 << '\t'
                 << s.travelStepElapsedMs / 1000 << '\t' << s.travelReplans << '\t'
                 << s.travelStepIndex << '\t' << s.travelStepCount << '\t' << s.activeQuestId << '\t'
                 << s.watchdogQuestId << '\t' << s.watchdogMs / 1000 << '\t' << s.deathRecoverySeconds << '\t'
                 << s.mapId << '\t' << s.zoneId << '\t' << s.areaId << '\t' << s.x << '\t' << s.y << '\t' << s.z << '\t'
                 << static_cast<uint32_t>(s.healthPct) << '\t' << static_cast<uint32_t>(s.manaPct) << '\t'
                 << s.dead << '\t' << s.inCombat << '\t' << s.snapshotReady << '\t' << Clean(s.movement) << '\t'
                 << s.hasPath << '\t' << Clean(s.pathFailure) << '\t' << s.pathFlags << '\t'
                 << s.pathAttemptGeneration << '\t' << s.pathEvidenceFresh << '\t'
                 << s.originPathFailures << '\t' << s.originPathDestinations << '\t'
                 << s.originRecoveryRequired << '\t'
                 << s.pathRequestX << '\t' << s.pathRequestY << '\t' << s.pathRequestZ << '\t'
                 << s.pathEndpointAvailable << '\t' << s.pathEndpointX << '\t'
                 << s.pathEndpointY << '\t' << s.pathEndpointZ << '\t'
                 << Clean(s.externalControl) << '\t' << s.navStuck << '\t'
                 << s.freeBagSlots << '\t' << s.totalBagSlots << '\t' << s.bagsFull << '\t' << s.needsRepair << '\t'
                 << s.needsRestock << '\t' << s.availableQuests << '\t' << s.activeQuests << '\t' << s.completedQuests << '\t'
                 << s.totalTransitions << '\t' << s.recentInterruptedTransitions << '\t' << s.churning;
            for (uint32_t count : s.soakTotals)
                body << '\t' << count;
            body << '\n';
        }

        const std::string contents = body.str();
        {
            std::ofstream latest(latestPath, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!latest)
            {
                TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not write latest progress snapshot '{}'.", latestPath);
                return;
            }
            latest << contents;
        }

        const bool writeHeader = IsEmptyOrMissing(historyPath);
        std::ofstream history(historyPath, std::ios::out | std::ios::app | std::ios::binary);
        if (!history)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Diagnostics] Could not append progress history '{}'.", historyPath);
            return;
        }
        if (writeHeader)
            history << Header;
        const size_t firstNewline = contents.find('\n');
        if (firstNewline != std::string::npos)
            history.write(contents.data() + firstNewline + 1,
                static_cast<std::streamsize>(contents.size() - firstNewline - 1));
    }

    void ProgressMonitor::Remove(uint32_t guidLow)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_progress.erase(guidLow);
    }

    void ProgressMonitor::Reset()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_progress.clear();
        s_enabled = false;
        s_latestPath.clear();
        s_historyPath.clear();
    }

    bool ProgressMonitor::IsEnabled()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_enabled;
    }

    std::string ProgressMonitor::GetLatestPath()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_latestPath;
    }

    std::string ProgressMonitor::GetHistoryPath()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_historyPath;
    }
}
