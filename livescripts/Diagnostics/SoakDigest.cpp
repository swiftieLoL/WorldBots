#include "SoakDigest.h"
#include "Log.h"

namespace Diagnostics
{
    std::unordered_map<uint32_t, SoakDigest::BotDigest> SoakDigest::s_digests;
    std::mutex SoakDigest::s_mutex;

    static const char* EventTag(SoakEvent e)
    {
        switch (e)
        {
            case SoakEvent::Deaths:              return "deaths";
            case SoakEvent::CircuitBreakerFired: return "circuit_breaks";
            case SoakEvent::QuestsSuppressed:    return "quest_suppress";
            case SoakEvent::NpcsSuppressed:      return "npc_suppress";
            case SoakEvent::GoalChurns:          return "churn";
            case SoakEvent::InventoryDeadlocks:  return "inv_deadlock";
            case SoakEvent::StuckEscalations:    return "stuck_escalate";
            case SoakEvent::ActionBugs:          return "action_bug";
            case SoakEvent::ActionsInterrupted:  return "interrupted";
            default: return "unknown";
        }
    }

    void SoakDigest::Record(uint32_t guidLow, SoakEvent event, uint32_t amount)
    {
        if (guidLow == 0)
            return;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto& digest = s_digests[guidLow];
        digest.counts[static_cast<size_t>(event)] += amount;
        digest.totals[static_cast<size_t>(event)] += amount;
    }

    uint32_t SoakDigest::GetTotal(uint32_t guidLow, SoakEvent event)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto digest = s_digests.find(guidLow);
        if (digest == s_digests.end())
            return 0;
        return digest->second.totals[static_cast<size_t>(event)];
    }

    void SoakDigest::Touch(uint32_t guidLow, const char* name, uint8_t level)
    {
        if (guidLow == 0)
            return;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto& d = s_digests[guidLow];
        d.name = name ? name : "";
        d.level = level;
    }

    void SoakDigest::EmitDigest()
    {
        struct OutputEntry
        {
            std::string name;
            uint8_t level;
            std::string events;
        };

        std::vector<OutputEntry> toEmit;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (auto& [guidLow, d] : s_digests)
            {
                // Build compact tag=count pairs for non-zero events only
                std::string events;
                for (size_t i = 0; i < EVENT_COUNT; ++i)
                {
                    if (d.counts[i] == 0)
                        continue;
                    if (!events.empty())
                        events += ' ';
                    events += EventTag(static_cast<SoakEvent>(i));
                    events += '=';
                    events += std::to_string(d.counts[i]);
                }

                if (events.empty())
                    continue;  // Quiet bots produce no output

                toEmit.push_back({ d.name, d.level, std::move(events) });

                // Reset counters for next window
                d.counts.fill(0);
            }
        }

        for (const auto& entry : toEmit)
        {
            TC_LOG_INFO("server", "[WorldBots] [SOAK] {} Lv{} | {}",
                entry.name, entry.level, entry.events);
        }
    }

    void SoakDigest::Remove(uint32_t guidLow)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_digests.erase(guidLow);
    }

    void SoakDigest::Clear()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_digests.clear();
    }
}
