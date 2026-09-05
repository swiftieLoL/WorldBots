#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Factory
{
    enum class BehaviorProfile : uint8_t
    {
        Balanced = 0,
        Cautious,
        Questing,
        Stress
    };

    struct BehaviorTuning
    {
        uint8_t fleeHealthPct = 20;
        uint8_t restHealthPct = 50;
        uint8_t restManaPct = 40;
    };

    struct BotDefinition
    {
        uint8_t race = 1;
        uint8_t playerClass = 1;
        uint8_t gender = 0;
        BehaviorProfile profile = BehaviorProfile::Balanced;

        bool operator==(const BotDefinition&) const = default;
    };

    // Roster entries use race:class:gender:profile and are comma-separated.
    // The all[:profile] shorthand expands to every playable WotLK race/class
    // combination with male gender and the selected (or balanced) profile.
    // Invalid entries are reported and ignored; an empty result means callers
    // should use their configured character defaults.
    std::vector<BotDefinition> ParseBotRoster(std::string_view value,
        std::vector<std::string>* errors = nullptr);

    bool IsPlayableRaceClassCombination(uint8_t race, uint8_t playerClass);
    std::vector<BotDefinition> CreateFullCoverageRoster(
        BehaviorProfile profile = BehaviorProfile::Balanced);

    BotDefinition SelectBotDefinition(const std::vector<BotDefinition>& roster,
        uint32_t slot, const BotDefinition& fallback);

    const char* BehaviorProfileName(BehaviorProfile profile);
    BehaviorTuning GetBehaviorTuning(BehaviorProfile profile);

    // Dedicated accounts use a reserved, normalized prefix and a fixed-width
    // base-36 slot suffix. The result always fits TrinityCore's 16-character
    // account-name limit.
    std::string NormalizeAccountPrefix(std::string_view prefix);
    std::string GenerateDedicatedAccountName(std::string_view prefix, uint32_t slot);
    bool HasDedicatedAccountPrefix(std::string_view accountName, std::string_view prefix);
}
