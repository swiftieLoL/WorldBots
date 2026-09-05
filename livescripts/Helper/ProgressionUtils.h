#pragma once

#include <cstdint>
#include <string>

class Player;

namespace Helper
{
    enum class CriticalSupplyDeficit : uint8_t
    {
        None = 0,
        Food = 1 << 0,
        Drink = 1 << 1,
        Potion = 1 << 2,
        Ammunition = 1 << 3,
        Reagent = 1 << 4
    };

    constexpr CriticalSupplyDeficit operator|(CriticalSupplyDeficit left, CriticalSupplyDeficit right)
    {
        return static_cast<CriticalSupplyDeficit>(
            static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr CriticalSupplyDeficit& operator|=(CriticalSupplyDeficit& left, CriticalSupplyDeficit right)
    {
        left = left | right;
        return left;
    }

    constexpr bool HasSupplyDeficit(CriticalSupplyDeficit value, CriticalSupplyDeficit flag)
    {
        return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
    }

    struct SupplyProvisionResult
    {
        CriticalSupplyDeficit before = CriticalSupplyDeficit::None;
        CriticalSupplyDeficit after = CriticalSupplyDeficit::None;
        uint32_t itemsCreated = 0;
        bool storageBlocked = false;

        bool MadeProgress() const { return before != after || itemsCreated > 0; }
        bool Complete() const { return after == CriticalSupplyDeficit::None; }
    };

    class ProgressionUtils
    {
    public:
        // Runs immediately on login and again when the bot levels. Unspent
        // talent points are retried even if the level did not change.
        static void MaintainCharacter(Player* bot, uint8_t& lastMaintainedLevel);

        // Supply shortages cover food, drink for mana users, compatible
        // ammunition for hunters, potions, and reagents for known spells.
        static CriticalSupplyDeficit GetCriticalSupplyDeficits(Player* bot);
        static std::string DescribeCriticalSupplyDeficits(CriticalSupplyDeficit deficits);
        static bool HasCriticalSupplyDeficit(Player* bot);
        static bool NeedsCriticalRestock(Player* bot);

        // Autonomous vendors are a service boundary rather than a simulation
        // of every individual merchant stock list. Once a bot reaches any
        // valid vendor, directly provision the missing critical supplies.
        static SupplyProvisionResult ProvisionCriticalSupplies(Player* bot);

        // Ensures all faction-appropriate flight paths from DBC are unlocked.
        // Idempotent: checks m_taxi and only updates unlearned nodes.
        static void EnsureFactionFlightPathsLearned(Player* bot);
    };
}
