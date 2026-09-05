#include "RestAction.h"
#include "Blackboard/BotBlackboard.h"
#include "Globals/ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Helper/InventoryUtils.h"
#include "Item.h"
#include "Bag.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"

namespace Actions
{
    RestAction::RestAction()
        : _consumeCooldownMs(0), _restTimerMs(0)
    {
    }

    void RestAction::Start(Player* bot, MovementManager* movement)
    {
        ResetOutcome();
        _consumeCooldownMs = 0;
        _restTimerMs = 0;
        if (!bot || !bot->IsInWorld())
        {
            Finish(ActionOutcome::RetryableFailure, "rest context was unavailable",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
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

    void RestAction::Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (_completed)
            return;

        if (!bot || !bot->IsInWorld())
        {
            Finish(ActionOutcome::RetryableFailure, "bot left the world while resting",
                FailureCategory::Transient, RecoveryDirective::RetryLater);
            return;
        }

        // Stand up immediately if combat begins or attackers are detected
        if (bot->IsInCombat() || blackboard.self.inCombat || !blackboard.combat.attackerGuids.empty())
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            Finish(ActionOutcome::Interrupted, "rest interrupted by combat",
                FailureCategory::Interaction, RecoveryDirective::Replan);
            return;
        }

        // Check if any hostile creature is within 15 yards to stand up before taking a 100% sitting crit
        for (ObjectGuid hostileGuid : blackboard.spatial.hostileGuids)
        {
            if (Unit* hostile = ObjectAccessor::GetUnit(*bot, hostileGuid))
            {
                if (hostile->IsAlive() && hostile->IsInWorld() && hostile->GetMap() == bot->GetMap() &&
                    bot->GetDistance(hostile) <= 15.0f)
                {
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    Finish(ActionOutcome::Interrupted, "rest interrupted by nearby approaching hostile",
                        FailureCategory::Interaction, RecoveryDirective::Replan);
                    return;
                }
            }
        }

        _restTimerMs += deltaMs;
        if (_restTimerMs >= 25000) // 25-second failsafe
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            Finish(ActionOutcome::Blocked, "vitals did not recover before the rest timeout",
                FailureCategory::ServiceCapability, RecoveryDirective::Replan);
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

                Helper::RecoveryConsumableRole role =
                    Helper::InventoryUtils::GetRecoveryConsumableRole(pProto);
                bool providesFood = role == Helper::RecoveryConsumableRole::Food ||
                    role == Helper::RecoveryConsumableRole::FoodAndDrink;
                bool providesDrink = role == Helper::RecoveryConsumableRole::Drink ||
                    role == Helper::RecoveryConsumableRole::FoodAndDrink;
                bool needsFood = hpPct < 85 && providesFood;
                bool needsDrink = isManaClass && manaPct < 85 && providesDrink;
                if (!needsFood && !needsDrink)
                    return true;

                for (uint8_t i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                {
                    if (pProto->Spells[i].SpellId > 0)
                    {
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
            Finish(ActionOutcome::Succeeded, "vitals recovered");
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
