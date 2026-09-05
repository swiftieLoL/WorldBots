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
    };

    inline RecoveryReserveSelection SelectRecoveryReserves(
        std::vector<RecoveryStackCandidate> const& candidates, bool needsDrink)
    {
        RecoveryReserveSelection selection;

        auto better = [](RecoveryStackCandidate const& candidate,
                          RecoveryStackCandidate const* current) {
            if (!current)
                return true;
            // Prefer larger stack if count difference is significant (e.g. >= 5 vs <= 2) and itemLevel difference is modest (<= 5)
            if (current->count >= 5 && candidate.count <= 2 && candidate.itemLevel <= current->itemLevel + 5)
                return false;
            if (candidate.count >= 5 && current->count <= 2 && current->itemLevel <= candidate.itemLevel + 5)
                return true;
            if (candidate.itemLevel != current->itemLevel)
                return candidate.itemLevel > current->itemLevel;
            if (candidate.count != current->count)
                return candidate.count > current->count;
            return candidate.position < current->position;
        };

        RecoveryStackCandidate const* food = nullptr;
        RecoveryStackCandidate const* drink = nullptr;
        RecoveryStackCandidate const* fallbackFood = nullptr;
        RecoveryStackCandidate const* fallbackDrink = nullptr;

        for (RecoveryStackCandidate const& candidate : candidates)
        {
            if (candidate.providesFood && better(candidate, fallbackFood))
                fallbackFood = &candidate;
            if (needsDrink && candidate.providesDrink && better(candidate, fallbackDrink))
                fallbackDrink = &candidate;

            if (!candidate.usable)
                continue;
            if (candidate.providesFood && better(candidate, food))
                food = &candidate;
            if (needsDrink && candidate.providesDrink && better(candidate, drink))
                drink = &candidate;
        }

        // If no currently usable stack was found, preserve the best fallback candidate
        // rather than leaving NoPosition (which would cause all food/drink to be sold).
        if (!food)
            food = fallbackFood;
        if (!drink)
            drink = fallbackDrink;

        if (food)
            selection.foodPosition = food->position;
        if (drink)
            selection.drinkPosition = drink->position;
        return selection;
    }
}
