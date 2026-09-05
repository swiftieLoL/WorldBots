#pragma once

class MovementManager;
class Player;
class Unit;

namespace Combat
{
    enum class RangeAdjustment : unsigned char
    {
        Hold,
        CloseDistance,
        CreateDistance
    };

    class CombatPositioning
    {
    public:
        static constexpr RangeAdjustment ChooseRangeAdjustment(
            float distance, float minimumRange, float maximumRange, bool hasLineOfSight,
            bool preserveActiveCast = false)
        {
            if (preserveActiveCast)
                return RangeAdjustment::Hold;
            if (!hasLineOfSight || distance > maximumRange)
                return RangeAdjustment::CloseDistance;
            if (minimumRange > 0.0f && distance < minimumRange)
                return RangeAdjustment::CreateDistance;
            return RangeAdjustment::Hold;
        }

        // Maintains a ranged firing band. When the target enters the minimum
        // range, request a short navmesh-backed retreat directly away from it.
        static RangeAdjustment MaintainRangeBand(Player* bot, Unit* target,
            MovementManager* movement, float minimumRange, float maximumRange);
    };
}
