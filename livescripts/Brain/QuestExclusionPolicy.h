#pragma once

#include <cstdint>

namespace Brain
{
    // Stable QuestSort.dbc IDs used by the WotLK quest template's negative
    // ZoneOrSort value. Keeping these local leaves this policy usable by the
    // server-independent logic test target.
    inline constexpr int32_t SeasonalQuestSort = 22;
    inline constexpr int32_t SpecialQuestSort = 284;
    inline constexpr int32_t DarkmoonFaireQuestSort = 364;
    inline constexpr int32_t AhnQirajWarQuestSort = 365;
    inline constexpr int32_t LunarFestivalQuestSort = 366;
    inline constexpr int32_t InvasionQuestSort = 368;
    inline constexpr int32_t MidsummerQuestSort = 369;
    inline constexpr int32_t BrewfestQuestSort = 370;
    inline constexpr int32_t NoblegardenQuestSort = 374;
    inline constexpr int32_t PilgrimsBountyQuestSort = 375;
    inline constexpr int32_t LoveIsInTheAirQuestSort = 376;

    // Quest::IsSeasonal() deliberately returns false for repeatable quests.
    // WorldBots rejects the entire authored seasonal category instead, so
    // holiday dailies and other repeatable event quests are excluded too.
    constexpr bool IsSeasonalQuestSort(int32_t zoneOrSort)
    {
        return zoneOrSort == -SeasonalQuestSort ||
            zoneOrSort == -SpecialQuestSort ||
            zoneOrSort == -LunarFestivalQuestSort ||
            zoneOrSort == -MidsummerQuestSort ||
            zoneOrSort == -BrewfestQuestSort ||
            zoneOrSort == -LoveIsInTheAirQuestSort ||
            zoneOrSort == -NoblegardenQuestSort ||
            zoneOrSort == -DarkmoonFaireQuestSort ||
            zoneOrSort == -AhnQirajWarQuestSort ||
            zoneOrSort == -InvasionQuestSort ||
            zoneOrSort == -PilgrimsBountyQuestSort;
    }

    // WorldBots deliberately does not participate in the classic Darkmoon
    // Faire hand-in economy. These quests are backed by rotating event NPCs,
    // while the static spawn cache cannot represent their live availability.
    inline bool IsPermanentlyExcludedQuest(uint32_t questId)
    {
        switch (questId)
        {
            case 7881: // Carnival Boots
            case 7882: // Carnival Jerkins
            case 7883: // The World's Largest Gnome!
            case 7884: // Crocolisk Boy and the Bearded Murloc
            case 7885: // Armor Kits
            case 7889: // Coarse Weightstone
            case 7890: // Heavy Grinding Stone
            case 7891: // Green Iron Bracers
            case 7892: // Big Black Mace
            case 7893: // Rituals of Strength
            case 7894: // Copper Modulator
            case 7895: // Whirring Bronze Gizmo
            case 7896: // Green Fireworks
            case 7897: // Mechanical Repair Kits
            case 7898: // Thorium Widget
            case 7899: // Small Furry Paws
            case 7900: // Torn Bear Pelts
            case 7901: // Soft Bushy Tails
            case 7902: // Vibrant Plumes
            case 7903: // Evil Bat Eyes
            case 7905: // The Darkmoon Faire
            case 7926: // The Darkmoon Faire
            case 7930: // 5 Tickets - Darkmoon Flower
            case 7931: // 5 Tickets - Minor Darkmoon Prize
            case 7932: // 12 Tickets - Lesser Darkmoon Prize
            case 7933: // 40 Tickets - Greater Darkmoon Prize
            case 7934: // 50 Tickets - Darkmoon Storage Box
            case 7935: // 10 Tickets - Last Month's Mutton
            case 7936: // 50 Tickets - Last Year's Mutton
            case 7939: // More Dense Grinding Stones
            case 7940: // 1200 Tickets - Orb of the Darkmoon
            case 7941: // More Armor Kits
            case 7942: // More Thorium Widgets
            case 7943: // More Bat Eyes
            case 7946: // Spawn of Jubjub
            case 7981: // 1200 Tickets - Amulet of the Darkmoon
            case 8222: // Glowing Scorpid Blood
            case 8223: // More Glowing Scorpid Blood
            case 9249: // 40 Tickets - Schematic: Steam Tonk Controller
                return true;
            default:
                return false;
        }
    }

    inline bool IsExcludedQuest(uint32_t questId, int32_t zoneOrSort)
    {
        return IsSeasonalQuestSort(zoneOrSort) ||
            IsPermanentlyExcludedQuest(questId);
    }
}
