#pragma once

#include <cstdint>

class Item;
struct ItemTemplate;
class Player;

namespace Helper
{
    enum class BotSpecialization : uint8_t
    {
        Arms,
        Retribution,
        Marksmanship,
        Combat,
        Shadow,
        Blood,
        Enhancement,
        Frost,
        Affliction,
        Feral,
        Generic
    };

    struct EquipmentUpgrade
    {
        uint8_t slot = 0;
        float candidateScore = 0.0f;
        float replacedScore = 0.0f;

        float Delta() const { return candidateScore - replacedScore; }
        bool IsUpgrade() const;
    };

    class EquipmentUtils
    {
    public:
        static BotSpecialization GetSpecialization(Player const* bot);
        static const char* GetSpecializationName(BotSpecialization specialization);

        // Produces one class/spec-aware value for equipment. It intentionally
        // uses only template data so inventory items, vendor items, and quest
        // rewards are all evaluated by the same policy.
        static float ScoreItem(Player const* bot, ItemTemplate const* itemTemplate);

        // Finds the legal equipment destination with the greatest score gain.
        // The Item overload preserves all normal soulbound/unique checks.
        static bool FindBestUpgrade(Player* bot, Item* item, EquipmentUpgrade& upgrade);
        static bool FindBestUpgrade(Player* bot, uint32_t itemId, EquipmentUpgrade& upgrade);

        // Upgrade rewards outrank cash rewards. If no choice is an upgrade,
        // usable equipment/consumables and finally vendor value break ties.
        static float ScoreQuestReward(Player* bot, uint32_t itemId);
    };
}
