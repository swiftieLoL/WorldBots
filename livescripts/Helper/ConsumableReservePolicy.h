#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace Helper
{
    struct RecoveryStackCandidate
    {
        uint16_t position = 0;
        uint32_t itemLevel = 0;
        uint32_t count = 0;
        bool providesFood = false;
        bool providesDrink = false;
        bool usable = false;
    };

    struct RecoveryReserveSelection
    {
        static constexpr uint16_t NoPosition = std::numeric_limits<uint16_t>::max();

        uint16_t foodPosition = NoPosition;
        uint16_t drinkPosition = NoPosition;

        bool Retains(uint16_t position) const
        {
            return position == foodPosition || position == drinkPosition;
        }
    };

    inline RecoveryReserveSelection SelectRecoveryReserves(
        std::vector<RecoveryStackCandidate> const& candidates, bool needsDrink)
    {
        RecoveryReserveSelection selection;

        auto better = [](RecoveryStackCandidate const& candidate,
                          RecoveryStackCandidate const* current) {
            if (!current)
                return true;
            if (candidate.itemLevel != current->itemLevel)
                return candidate.itemLevel > current->itemLevel;
            if (candidate.count != current->count)
                return candidate.count > current->count;
            return candidate.position < current->position;
        };

        RecoveryStackCandidate const* food = nullptr;
        RecoveryStackCandidate const* drink = nullptr;
        for (RecoveryStackCandidate const& candidate : candidates)
        {
            if (!candidate.usable)
                continue;
            if (candidate.providesFood && better(candidate, food))
                food = &candidate;
            if (needsDrink && candidate.providesDrink && better(candidate, drink))
                drink = &candidate;
        }

        if (food)
            selection.foodPosition = food->position;
        if (drink)
            selection.drinkPosition = drink->position;
        return selection;
    }
}
