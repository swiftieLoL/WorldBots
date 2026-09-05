#include "HunterStrategy.h"

#include "ClassStrategyUtils.h"
#include "HunterCombatPolicy.h"
#include "Combat/CombatPositioning.h"
#include "Helper/HunterPetManagementPolicy.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "Pet.h"
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

    void HunterStrategy::ExecuteCombat(Player* bot, Unit* target, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        ObjectGuid targetGuid = target->GetGUID();
        ClassStrategyUtils::EngagePet(bot, target);

        if (!bot->GetPet() && _decisionTimer.IsReady() &&
            ClassStrategyUtils::TryCastRank(bot, bot, 883, StrategyName, "Call Pet"))
        {
            _decisionTimer.Set(1500);
            return;
        }

        Pet* pet = bot->GetPet();
        if (pet && _decisionTimer.IsReady() &&
            Helper::HunterPetManagementPolicy::ShouldMend(
                true, pet->IsAlive(),
                pet->HealthBelowPct(Helper::HunterPetManagementPolicy::MendHealthPct),
                Helper::SpellUtils::HasAuraInChain(pet, 136, bot->GetGUID())) &&
            ClassStrategyUtils::TryCastRank(bot, pet, 136, StrategyName, "Mend Pet"))
        {
            _decisionTimer.Set(1500);
            return;
        }

        float distance = bot->GetDistance(target);
        bool canShoot = HasUsableAmmunition(bot);

        // Auto-shot cannot handle the close-range dead zone. Switch to normal
        // melee combat until the target moves back into ranged weapon range.
        if (HunterCombatPolicy::ShouldUseMelee(distance, canShoot))
        {
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
            if (!ClassStrategyUtils::MaintainMelee(bot, target, movement) ||
                bot->IsNonMeleeSpellCast(false) || !_decisionTimer.IsReady())
                return;

            if (!Helper::SpellUtils::HasAuraInChain(target, 2974) &&
                ClassStrategyUtils::TryCastRank(bot, target, 2974, StrategyName, "Wing Clip"))
            {
                _decisionTimer.Set(1000);
                return;
            }

            if (ClassStrategyUtils::TryCastRank(bot, target, 2973, StrategyName, "Raptor Strike"))
                _decisionTimer.Set(1000);
            return;
        }

        RangeAdjustment range = CombatPositioning::MaintainRangeBand(bot, target, movement,
            HunterCombatPolicy::MinimumRangedDistance,
            HunterCombatPolicy::MaximumReliableRangedDistance);
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

        if (ClassStrategyUtils::TryMaintainAura(bot, target, 1130, StrategyName, "Hunter's Mark", _decisionTimer))
            return;

        if (target->HealthBelowPct(20) &&
            ClassStrategyUtils::TryCastRank(bot, target, 53351, StrategyName, "Kill Shot"))
        {
            _decisionTimer.Set(1000);
            return;
        }

        if (ClassStrategyUtils::TryMaintainAura(bot, target, 1978, StrategyName, "Serpent Sting", _decisionTimer))
            return;

        static constexpr ClassStrategyUtils::PrioritySpell prioritySpells[] = {
            { 53209, "Chimera Shot", 1000 },
            { 53301, "Explosive Shot", 1000 },
            { 19434, "Aimed Shot", 1000 },
            { 3044, "Arcane Shot", 1000 },
            { 56641, "Steady Shot", 1000 }
        };
        if (ClassStrategyUtils::TryCastPriorityList(bot, target, prioritySpells, StrategyName, _decisionTimer))
            return;

        target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (target && target->IsAlive())
            ClassStrategyUtils::EnsureAutoShot(bot, target);
    }

    bool HunterStrategy::ExecuteDisengageCC(
        Player* bot,
        Unit* threat,
        const Blackboard::BotBlackboard& /*blackboard*/)
    {
        float dist = bot->GetDistance(threat);

        if (bot->IsWithinMeleeRange(threat) && ClassStrategyUtils::TryCastRank(bot, threat, 2974, GetName(), "Wing Clip"))
            return true;

        if (dist <= 10.0f && bot->HasSpell(781) && ClassStrategyUtils::TryCast(bot, threat, 781, GetName(), "Disengage"))
            return true;

        if (dist >= 8.0f && dist <= 35.0f && ClassStrategyUtils::TryCastRank(bot, threat, 5116, GetName(), "Concussive Shot"))
            return true;

        return false;
    }
}
