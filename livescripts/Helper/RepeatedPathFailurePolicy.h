#pragma once

#include <cstdint>

namespace Helper::RepeatedPathFailurePolicy
{
    constexpr std::uint32_t FailureLimit = 3;

    template <typename Key>
    class Tracker
    {
    public:
        bool Observe(const Key& key, bool freshAttempt, bool failed)
        {
            if (!freshAttempt)
                return false;

            if (!failed)
            {
                Reset();
                return false;
            }

            if (!_hasKey || key != _key)
            {
                _key = key;
                _failureCount = 1;
                _hasKey = true;
            }
            else
            {
                ++_failureCount;
            }

            return _failureCount >= FailureLimit;
        }

        void Reset()
        {
            _key = {};
            _failureCount = 0;
            _hasKey = false;
        }

        std::uint32_t GetFailureCount() const { return _failureCount; }

    private:
        Key _key{};
        std::uint32_t _failureCount = 0;
        bool _hasKey = false;
    };
}
