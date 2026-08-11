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
        // Maintains a visible ranged firing position and returns whether the
        // target currently has line of sight.
        static bool MaintainRanged(Player* bot, Unit* target, MovementManager* movement, float maximumRange);

        static constexpr RangeAdjustment ChooseRangeAdjustment(
            float distance, float minimumRange, float maximumRange, bool hasLineOfSight)
        {
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
