#pragma once

#include <algorithm>
#include <cstdint>

namespace Brain
{
    constexpr uint32_t FleeCombatClearGraceMs = 1500;
    constexpr uint8_t StandardFleeHealthPct = 20;

    inline bool IsCriticalFleeHealth(uint8_t healthPct)
    {
        return healthPct <= StandardFleeHealthPct;
    }

    struct FleeCompletionInput
    {
        bool botInCombat = false;
        bool blackboardInCombat = false;
        bool hasActiveAttackers = false;
        bool threatEngaged = false;
        bool threatSafelySeparated = true;
        bool movementIdle = false;
        uint32_t combatClearMs = 0;
    };

    inline bool IsFleeCombatClear(const FleeCompletionInput& input)
    {
        return !input.botInCombat && !input.blackboardInCombat &&
            !input.hasActiveAttackers && !input.threatEngaged;
    }

    inline bool ShouldCompleteFleeAfterCombatDrop(const FleeCompletionInput& input)
    {
        return input.movementIdle && input.threatSafelySeparated &&
            IsFleeCombatClear(input) &&
            input.combatClearMs >= FleeCombatClearGraceMs;
    }

    enum class CombatArmorTier : uint8_t
    {
        Cloth = 0,
        Leather,
        MailOrPlate
    };

    inline CombatArmorTier GetArmorTierForClass(uint8_t playerClass)
    {
        switch (playerClass)
        {
            case 8: // Mage
            case 5: // Priest
            case 9: // Warlock
                return CombatArmorTier::Cloth;
            case 4:  // Rogue
            case 11: // Druid
            case 3:  // Hunter
                return CombatArmorTier::Leather;
            case 1: // Warrior
            case 2: // Paladin
            case 7: // Shaman
            case 6: // Death Knight
            default:
                return CombatArmorTier::MailOrPlate;
        }
    }

    inline uint8_t CalculateMultiAttackerFleeThreshold(
        CombatArmorTier armorTier,
        uint32_t attackerCount,
        int32_t highestLevelDelta)
    {
        if (attackerCount < 2)
            return StandardFleeHealthPct;

        if (attackerCount >= 4)
            return 85;

        if (attackerCount == 3)
        {
            if (highestLevelDelta >= 2)
            {
                switch (armorTier)
                {
                    case CombatArmorTier::Cloth: return 75;
                    case CombatArmorTier::Leather: return 70;
                    case CombatArmorTier::MailOrPlate: return 60;
                }
            }
            else if (highestLevelDelta >= 0)
            {
                switch (armorTier)
                {
                    case CombatArmorTier::Cloth: return 65;
                    case CombatArmorTier::Leather: return 55;
                    case CombatArmorTier::MailOrPlate: return 45;
                }
            }
            else
            {
                switch (armorTier)
                {
                    case CombatArmorTier::Cloth: return 50;
                    case CombatArmorTier::Leather: return 40;
                    case CombatArmorTier::MailOrPlate: return 35;
                }
            }
        }

        // 2 attackers
        if (highestLevelDelta >= 2)
        {
            switch (armorTier)
            {
                case CombatArmorTier::Cloth: return 65;
                case CombatArmorTier::Leather: return 55;
                case CombatArmorTier::MailOrPlate: return 45;
            }
        }
        else if (highestLevelDelta >= 0)
        {
            switch (armorTier)
            {
                case CombatArmorTier::Cloth: return 50;
                case CombatArmorTier::Leather: return 40;
                case CombatArmorTier::MailOrPlate: return 35;
            }
        }
        else
        {
            switch (armorTier)
            {
                case CombatArmorTier::Cloth: return 35;
                case CombatArmorTier::Leather: return 30;
                case CombatArmorTier::MailOrPlate: return 25;
            }
        }

        return StandardFleeHealthPct;
    }

    inline uint8_t CalculateAboveLevelAttackerFleeThreshold(
        CombatArmorTier armorTier,
        int32_t levelDelta)
    {
        if (levelDelta >= 3)
            return 85;

        if (levelDelta == 2)
        {
            switch (armorTier)
            {
                case CombatArmorTier::Cloth: return 50;
                case CombatArmorTier::Leather: return 40;
                case CombatArmorTier::MailOrPlate: return 30;
            }
        }

        if (levelDelta == 1)
        {
            switch (armorTier)
            {
                case CombatArmorTier::Cloth: return 30;
                case CombatArmorTier::Leather: return 25;
                case CombatArmorTier::MailOrPlate: return 20;
            }
        }

        return StandardFleeHealthPct;
    }

    struct MultiAttackerRiskInput
    {
        uint32_t botLevel = 1;
        uint32_t highestAttackerLevel = 1;
        uint32_t attackerCount = 0;
        uint8_t healthPct = 100;
        uint8_t playerClass = 0;
    };

    inline bool ShouldFleeFromMultipleAttackers(const MultiAttackerRiskInput& input)
    {
        if (input.attackerCount < 2)
            return false;

        int32_t highestLevelDelta = static_cast<int32_t>(input.highestAttackerLevel) -
            static_cast<int32_t>(input.botLevel);

        if (highestLevelDelta >= 3)
            return true;

        CombatArmorTier tier = GetArmorTierForClass(input.playerClass);
        uint8_t threshold = CalculateMultiAttackerFleeThreshold(
            tier, input.attackerCount, highestLevelDelta);

        return input.healthPct <= threshold;
    }

    struct AboveLevelAttackerRiskInput
    {
        uint32_t botLevel = 1;
        uint32_t attackerLevel = 1;
        uint8_t healthPct = 100;
        bool engaged = false;
        uint8_t playerClass = 0;
    };

    inline bool ShouldFleeFromAboveLevelAttacker(
        const AboveLevelAttackerRiskInput& input)
    {
        if (!input.engaged)
            return false;

        int32_t levelDelta = static_cast<int32_t>(input.attackerLevel) -
            static_cast<int32_t>(input.botLevel);

        if (levelDelta >= 3)
            return true;

        CombatArmorTier tier = GetArmorTierForClass(input.playerClass);
        uint8_t threshold = CalculateAboveLevelAttackerFleeThreshold(
            tier, levelDelta);

        return input.healthPct <= threshold;
    }

    inline int32_t SelectAmbientThreatMaxLevelOffset(bool questContext,
        int32_t grindMaxLevelOffset, int32_t questMaxLevelsAboveBot)
    {
        // The grind band controls which creatures the bot proactively hunts.
        // It must not make a creature that is still inside the configured solo
        // quest allowance look overwhelming merely because it is nearby.
        return questContext
            ? std::max(grindMaxLevelOffset, questMaxLevelsAboveBot)
            : grindMaxLevelOffset;
    }

    struct CombatInitiationInput
    {
        uint32_t botLevel = 1;
        uint32_t targetLevel = 1;
        int32_t maxLevelOffset = 0;
        bool targetEngagedWithBot = false;
        bool targetExecutable = true;
    };

    inline bool ShouldInitiateCombat(const CombatInitiationInput& input)
    {
        // Trinity can retain combat/attacker bookkeeping while a creature is
        // evading or otherwise invalid. Engagement alone must not create an
        // action that live validation will reject on its first update.
        if (!input.targetExecutable)
            return false;

        // Once a creature is attacking the bot this is no longer a voluntary
        // initiation decision; the tactical layer may still choose to flee.
        if (input.targetEngagedWithBot)
            return true;

        int32_t maximumLevel = std::max<int32_t>(1,
            static_cast<int32_t>(input.botLevel) + input.maxLevelOffset);
        return static_cast<int32_t>(input.targetLevel) <= maximumLevel;
    }

    inline bool ShouldFleeForThreat(bool lowHealthEscape, bool hasCombatActionTarget,
        bool hasOverwhelmingThreat)
    {
        return (lowHealthEscape && hasCombatActionTarget) || hasOverwhelmingThreat;
    }

    inline bool ShouldAttributeThreatToProactiveRoute(bool hasOverwhelmingThreat,
        bool threatEngaged)
    {
        return hasOverwhelmingThreat && threatEngaged;
    }
}
