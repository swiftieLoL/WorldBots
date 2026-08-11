#include "HunterStrategy.h"

#include "ClassStrategyUtils.h"
#include "Combat/CombatPositioning.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "Spell.h"

namespace Combat
{
    namespace
    {
        constexpr char StrategyName[] = "Hunter";

        bool HasUsableAmmunition(Player* bot)
        {
            Item* rangedWeapon = bot ? bot->GetWeaponForAttack(RANGED_ATTACK, true) : nullptr;
            if (!rangedWeapon || !rangedWeapon->GetTemplate())
                return false;

            if (rangedWeapon->GetTemplate()->InventoryType == INVTYPE_THROWN)
                return bot->HasItemCount(rangedWeapon->GetEntry());

            uint32_t ammoId = bot->GetUInt32Value(PLAYER_AMMO_ID);
            return ammoId != 0 && bot->HasItemCount(ammoId);
        }
    }

    void HunterStrategy::UpdateCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs)
    {
        if (!ClassStrategyUtils::IsValid(bot, target))
            return;

        ObjectGuid targetGuid = target->GetGUID();
        _decisionTimer.Tick(deltaMs);
        ClassStrategyUtils::EngagePet(bot, target);

        if (!bot->GetPet() && _decisionTimer.IsReady() &&
            ClassStrategyUtils::TryCastRank(bot, bot, 883, StrategyName, "Call Pet"))
        {
            _decisionTimer.Set(1500);
            return;
        }

        float distance = bot->GetDistance(target);
        bool canShoot = HasUsableAmmunition(bot);

        // The reference playerbots implementation prioritizes a snare and
        // disengage when a target enters the auto-shot dead zone.
        if (distance < 8.0f)
        {
            if (_decisionTimer.IsReady() && !Helper::SpellUtils::HasAuraInChain(target, 2974) &&
                ClassStrategyUtils::TryCastRank(bot, target, 2974, StrategyName, "Wing Clip"))
            {
                _decisionTimer.Set(1000);
                return;
            }
            if (_decisionTimer.IsReady() &&
                ClassStrategyUtils::TryCastRank(bot, target, 781, StrategyName, "Disengage"))
            {
                _decisionTimer.Set(1000);
                return;
            }

            CombatPositioning::MaintainRangeBand(bot, target, movement, 8.0f, 30.0f);
            if (!canShoot)
            {
                if (ClassStrategyUtils::MaintainMelee(bot, target, movement) && _decisionTimer.IsReady() &&
                    ClassStrategyUtils::TryCastRank(bot, target, 2973, StrategyName, "Raptor Strike"))
                    _decisionTimer.Set(1000);
            }
            return;
        }

        if (!canShoot)
        {
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
            if (ClassStrategyUtils::MaintainMelee(bot, target, movement) && _decisionTimer.IsReady() &&
                ClassStrategyUtils::TryCastRank(bot, target, 2973, StrategyName, "Raptor Strike"))
                _decisionTimer.Set(1000);
            return;
        }

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement, 8.0f, 30.0f);
        if (range != RangeAdjustment::Hold || !bot->IsWithinLOSInMap(target))
            return;

        if (bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
        {
            ClassStrategyUtils::EnsureAutoShot(bot, target);
            return;
        }

        // Use Viper only while mana-starved, then return to the strongest
        // damage aspect available. Aspects are mutually exclusive in core.
        if (blackboard.self.manaPct < 20 && !Helper::SpellUtils::HasAuraInChain(bot, 34074) &&
            ClassStrategyUtils::TryCastRank(bot, bot, 34074, StrategyName, "Aspect of the Viper"))
        {
            _decisionTimer.Set(1000);
            return;
        }
        if (blackboard.self.manaPct >= 40 &&
            !Helper::SpellUtils::HasAuraInChain(bot, 61846) &&
            !Helper::SpellUtils::HasAuraInChain(bot, 13165))
        {
            if (ClassStrategyUtils::TryCastRank(bot, bot, 61846, StrategyName, "Aspect of the Dragonhawk") ||
                ClassStrategyUtils::TryCastRank(bot, bot, 13165, StrategyName, "Aspect of the Hawk"))
            {
                _decisionTimer.Set(1000);
                return;
            }
        }

        if (!Helper::SpellUtils::HasAuraInChain(target, 1130) &&
            ClassStrategyUtils::TryCastRank(bot, target, 1130, StrategyName, "Hunter's Mark"))
        {
            _decisionTimer.Set(1000);
            return;
        }
        if (target->HealthBelowPct(20) &&
            ClassStrategyUtils::TryCastRank(bot, target, 53351, StrategyName, "Kill Shot"))
        {
            _decisionTimer.Set(1000);
            return;
        }
        if (!Helper::SpellUtils::HasAuraInChain(target, 1978) &&
            ClassStrategyUtils::TryCastRank(bot, target, 1978, StrategyName, "Serpent Sting"))
        {
            _decisionTimer.Set(1000);
            return;
        }

        static constexpr uint32_t priority[] = { 53209, 53301, 19434, 3044, 56641 };
        static constexpr const char* names[] = { "Chimera Shot", "Explosive Shot", "Aimed Shot", "Arcane Shot", "Steady Shot" };
        for (size_t index = 0; index < std::size(priority); ++index)
        {
            if (ClassStrategyUtils::TryCastRank(bot, target, priority[index], StrategyName, names[index]))
            {
                _decisionTimer.Set(1000);
                return;
            }
        }

        target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (target && target->IsAlive())
            ClassStrategyUtils::EnsureAutoShot(bot, target);
    }
}
