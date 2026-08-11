#include "BotRoster.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>

namespace Factory
{
    namespace
    {
        std::string_view Trim(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
            return value;
        }

        bool ParseByte(std::string_view value, uint8_t& result)
        {
            value = Trim(value);
            uint32_t parsed = 0;
            const char* begin = value.data();
            const char* end = begin + value.size();
            auto conversion = std::from_chars(begin, end, parsed);
            if (conversion.ec != std::errc{} || conversion.ptr != end || parsed > std::numeric_limits<uint8_t>::max())
                return false;
            result = static_cast<uint8_t>(parsed);
            return true;
        }

        bool IsPlayableRace(uint8_t race)
        {
            switch (race)
            {
                case 1:  // Human
                case 2:  // Orc
                case 3:  // Dwarf
                case 4:  // Night Elf
                case 5:  // Undead
                case 6:  // Tauren
                case 7:  // Gnome
                case 8:  // Troll
                case 10: // Blood Elf
                case 11: // Draenei
                    return true;
                default:
                    return false;
            }
        }

        bool IsPlayableClass(uint8_t playerClass)
        {
            switch (playerClass)
            {
                case 1:  // Warrior
                case 2:  // Paladin
                case 3:  // Hunter
                case 4:  // Rogue
                case 5:  // Priest
                case 6:  // Death Knight
                case 7:  // Shaman
                case 8:  // Mage
                case 9:  // Warlock
                case 11: // Druid
                    return true;
                default:
                    return false;
            }
        }

        bool ParseBehaviorProfile(std::string_view value, BehaviorProfile& profile)
        {
            std::string normalized(Trim(value));
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (normalized == "balanced")
                profile = BehaviorProfile::Balanced;
            else if (normalized == "cautious")
                profile = BehaviorProfile::Cautious;
            else if (normalized == "questing")
                profile = BehaviorProfile::Questing;
            else if (normalized == "stress")
                profile = BehaviorProfile::Stress;
            else
                return false;
            return true;
        }

        std::string EncodeBase36(uint64_t value)
        {
            constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            std::string encoded;
            do
            {
                encoded.push_back(digits[value % digits.size()]);
                value /= digits.size();
            } while (value != 0);
            std::reverse(encoded.begin(), encoded.end());
            return encoded;
        }
    }

    std::vector<BotDefinition> ParseBotRoster(std::string_view value,
        std::vector<std::string>* errors)
    {
        std::vector<BotDefinition> result;
        uint32_t entryNumber = 0;
        while (!value.empty())
        {
            size_t separator = value.find(',');
            std::string_view entry = Trim(value.substr(0, separator));
            value = separator == std::string_view::npos ? std::string_view{} : value.substr(separator + 1);
            ++entryNumber;

            if (entry.empty())
                continue;

            std::array<std::string_view, 4> fields{};
            size_t fieldCount = 0;
            while (!entry.empty() && fieldCount < fields.size())
            {
                size_t colon = entry.find(':');
                fields[fieldCount++] = entry.substr(0, colon);
                entry = colon == std::string_view::npos ? std::string_view{} : entry.substr(colon + 1);
            }

            BotDefinition definition;
            bool valid = fieldCount == fields.size() && entry.empty() &&
                ParseByte(fields[0], definition.race) &&
                ParseByte(fields[1], definition.playerClass) &&
                ParseByte(fields[2], definition.gender) &&
                ParseBehaviorProfile(fields[3], definition.profile) &&
                IsPlayableRace(definition.race) && IsPlayableClass(definition.playerClass) &&
                definition.gender <= 1;

            if (!valid)
            {
                if (errors)
                    errors->push_back("roster entry " + std::to_string(entryNumber) +
                        " must be race:class:gender:balanced|cautious|questing|stress using playable WotLK IDs");
                continue;
            }

            result.push_back(definition);
        }
        return result;
    }

    BotDefinition SelectBotDefinition(const std::vector<BotDefinition>& roster,
        uint32_t slot, const BotDefinition& fallback)
    {
        return roster.empty() ? fallback : roster[slot % roster.size()];
    }

    const char* BehaviorProfileName(BehaviorProfile profile)
    {
        switch (profile)
        {
            case BehaviorProfile::Cautious: return "cautious";
            case BehaviorProfile::Questing: return "questing";
            case BehaviorProfile::Stress: return "stress";
            case BehaviorProfile::Balanced:
            default: return "balanced";
        }
    }

    BehaviorTuning GetBehaviorTuning(BehaviorProfile profile)
    {
        switch (profile)
        {
            case BehaviorProfile::Cautious: return { 30, 65, 55 };
            case BehaviorProfile::Questing: return { 15, 40, 30 };
            case BehaviorProfile::Stress: return { 5, 25, 20 };
            case BehaviorProfile::Balanced:
            default: return { 20, 50, 40 };
        }
    }

    std::string NormalizeAccountPrefix(std::string_view prefix)
    {
        std::string result;
        result.reserve(9);
        for (unsigned char c : prefix)
        {
            if (std::isalnum(c))
                result.push_back(static_cast<char>(std::toupper(c)));
            if (result.size() == 9)
                break;
        }
        return result.empty() ? "WBOT" : result;
    }

    std::string GenerateDedicatedAccountName(std::string_view prefix, uint32_t slot)
    {
        std::string normalized = NormalizeAccountPrefix(prefix);
        std::string suffix = EncodeBase36(static_cast<uint64_t>(slot) + 1);
        if (suffix.size() < 7)
            suffix.insert(suffix.begin(), 7 - suffix.size(), '0');
        return normalized + suffix;
    }

    bool HasDedicatedAccountPrefix(std::string_view accountName, std::string_view prefix)
    {
        std::string normalized = NormalizeAccountPrefix(prefix);
        if (accountName.size() < normalized.size())
            return false;
        for (size_t i = 0; i < normalized.size(); ++i)
        {
            if (std::toupper(static_cast<unsigned char>(accountName[i])) != normalized[i])
                return false;
        }
        return true;
    }
}
