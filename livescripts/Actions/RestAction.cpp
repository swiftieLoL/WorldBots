#include "RestAction.h"
#include "Helper/InventoryUtils.h"
#include "Item.h"
#include "Bag.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"

namespace Actions
{
    RestAction::RestAction()
        : _completed(false), _consumeCooldownMs(0), _restTimerMs(0)
    {
    }

    void RestAction::Start(Player* bot, MovementManager* movement)
    {
        _completed = false;
        _consumeCooldownMs = 0;
        _restTimerMs = 0;
        if (!bot || !bot->IsInWorld())
        {
            _completed = true;
            return;
        }

        if (movement)
        {
            movement->Stop();
        }

        if (!bot->IsSitState())
        {
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            uint32_t curHpPct = bot->GetHealthPct();
            uint32_t maxMana = bot->GetMaxPower(POWER_MANA);
            uint32_t curManaPct = (maxMana > 0) ? (bot->GetPower(POWER_MANA) * 100 / maxMana) : 100;

            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' (GUID: {}) sitting down out-of-combat to recover vitals (HP: {}%, Mana: {}%)...",
                    bot->GetName(), bot->GetGUID().GetCounter(), curHpPct, curManaPct);
        }
    }

    void RestAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& /*blackboard*/, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld())
        {
            _completed = true;
            return;
        }

        _restTimerMs += deltaMs;
        if (_restTimerMs >= 25000) // 25-second failsafe
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            _completed = true;
            return;
        }

        if (movement)
        {
            movement->Stop();
        }

        bool isManaClass = (bot->GetMaxPower(POWER_MANA) > 0);
        uint32_t hpPct = bot->GetHealthPct();
        uint32_t maxMana = bot->GetMaxPower(POWER_MANA);
        uint32_t manaPct = (maxMana > 0) ? (bot->GetPower(POWER_MANA) * 100 / maxMana) : 100;

        if (_consumeCooldownMs > deltaMs)
        {
            _consumeCooldownMs -= deltaMs;
        }
        else
        {
            _consumeCooldownMs = 0;
        }

        // Check for food / drink items in inventory using InventoryUtils if debounce cooldown expired and not casting
        if (_consumeCooldownMs == 0 && !bot->HasUnitState(UNIT_STATE_CASTING))
        {
            Helper::InventoryUtils::ForEachBagItem(bot, [&](uint8_t /*bag*/, uint8_t /*slot*/, Item* pItem) -> bool {
                if (!pItem) return true;
                ItemTemplate const* pProto = pItem->GetTemplate();
                if (!pProto || pProto->Class != ITEM_CLASS_CONSUMABLE) return true;

                for (uint8_t i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                {
                    if (pProto->Spells[i].SpellId > 0)
                    {
                        if (!isManaClass && hpPct >= 85)
                            continue;

                        bot->CastSpell(bot, pProto->Spells[i].SpellId, false);
                        if (Diagnostics::BotTrace::ShouldLog(bot))
                            TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' consumed '{}' (Spell: {}) for rapid vital recovery.",
                                bot->GetName(), pProto->Name1, pProto->Spells[i].SpellId);

                        _consumeCooldownMs = 5000;
                        return false; // Stop iteration once item is consumed
                    }
                }
                return true;
            });
        }

        // Recovery thresholds (HP >= 85%, Mana >= 85% for mana classes)
        bool healthRecovered = hpPct >= 85;
        bool manaRecovered = (!isManaClass) || (manaPct >= 85);

        if (healthRecovered && manaRecovered)
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            if (Diagnostics::BotTrace::ShouldLog(bot))
                TC_LOG_INFO("server", "[WorldBots] [Brain] Bot '{}' fully recovered vitals (HP: {}%, Mana: {}%). Resuming tasks.",
                    bot->GetName(), hpPct, manaPct);
            _completed = true;
        }
    }

    void RestAction::Stop(Player* bot, MovementManager* /*movement*/)
    {
        if (bot && bot->IsInWorld())
        {
            if (bot->IsSitState())
            {
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            }
        }
    }
}
