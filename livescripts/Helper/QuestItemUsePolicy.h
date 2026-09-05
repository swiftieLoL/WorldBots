#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Helper
{
    struct QuestItemUseSpellSlot
    {
        uint32_t spellId = 0;
        bool onUse = false;
        bool hasSpellInfo = false;
    };

    template <std::size_t SlotCount>
    constexpr uint32_t SelectFirstValidQuestItemUseSpell(
        const std::array<QuestItemUseSpellSlot, SlotCount>& slots)
    {
        for (const QuestItemUseSpellSlot& slot : slots)
        {
            if (slot.spellId != 0 && slot.onUse && slot.hasSpellInfo)
                return slot.spellId;
        }
        return 0;
    }

    // Quest automation may use the normal Player item-use path only when the
    // condition resolver selected the first valid spell TrinityCore would
    // select. Unknown ON_USE spell IDs are skipped by core.
    // Scripted items stay unsupported because their OnItemUse hook cannot be
    // invoked safely without a real client opcode.
    inline bool IsSupportedQuestItemUse(uint32_t itemScriptId,
        uint32_t firstOnUseSpellId, uint32_t resolvedSpellId)
    {
        return itemScriptId == 0 && firstOnUseSpellId != 0 &&
            firstOnUseSpellId == resolvedSpellId;
    }
}
