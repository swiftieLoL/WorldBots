#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Brain
{
    // Stores keys with an absolute expiry supplied by the owning clock.  The
    // container deliberately does not read a global clock, which keeps it
    // reusable in deterministic policy tests and with either ms or sec units.
    template <typename Key>
    class TimedBlacklist
    {
    public:
        using EntryMap = std::unordered_map<Key, uint32_t>;

        void AddUntil(const Key& key, uint32_t expiry)
        {
            if (key != Key{})
                _entries[key] = expiry;
        }

        void Add(const Key& key, uint32_t duration, uint32_t now)
        {
            AddUntil(key, now + duration);
        }

        bool Contains(const Key& key) const
        {
            return _entries.find(key) != _entries.end();
        }

        bool Contains(const Key& key, uint32_t now) const
        {
            auto it = _entries.find(key);
            return it != _entries.end() && now < it->second;
        }

        uint32_t GetRemaining(const Key& key, uint32_t now) const
        {
            auto it = _entries.find(key);
            return it != _entries.end() && now < it->second ? it->second - now : 0;
        }

        void Cleanup(uint32_t now)
        {
            for (auto it = _entries.begin(); it != _entries.end(); )
            {
                if (now >= it->second)
                    it = _entries.erase(it);
                else
                    ++it;
            }
        }

        std::vector<std::pair<Key, uint32_t>> GetRemainingEntries(uint32_t now) const
        {
            std::vector<std::pair<Key, uint32_t>> result;
            result.reserve(_entries.size());
            for (const auto& [key, expiry] : _entries)
                if (expiry > now)
                    result.emplace_back(key, expiry - now);
            return result;
        }

        const EntryMap& Entries() const { return _entries; }

    private:
        EntryMap _entries;
    };
}
