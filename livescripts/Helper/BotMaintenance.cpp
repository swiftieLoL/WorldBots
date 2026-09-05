#include "BotMaintenance.h"

#include "Helper/HunterPetManagementPolicy.h"
#include "Helper/HunterPetProvisioningPolicy.h"
#include "Helper/ProgressionUtils.h"
#include "Helper/SpellLearningUtils.h"
#include "Helper/SpellUtils.h"
#include "Log.h"
#include "Map.h"
#include "Pet.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace Helper
{
    namespace
    {
        constexpr uint32_t CallPetSpell = 883;
        constexpr uint32_t RevivePetSpell = 982;
        constexpr uint32_t MendPetSpell = 136;
        constexpr uint32_t CowerSpell = 1742;

        void ProvisionHunterPet(Player* bot, bool& provisionAttempted)
        {
            if (!bot || bot->GetClass() != CLASS_HUNTER ||
                bot->GetLevel() < HunterPetProvisioningPolicy::MinimumLevel)
                return;

            // These abilities normally arrive through the level-10 taming
            // quest chain, which managed bots intentionally do not run.
            if (!bot->HasSpell(CallPetSpell))
                bot->LearnSpell(CallPetSpell, false);
            if (!bot->HasSpell(RevivePetSpell))
                bot->LearnSpell(RevivePetSpell, false);

            PetStable& stable = bot->GetOrInitPetStable();
            bool hasActivePet = !bot->GetPetGUID().IsEmpty();
            bool hasSavedHunterPet = stable.CurrentPet || stable.GetUnslottedHunterPet();

            if (hasActivePet || hasSavedHunterPet)
            {
                provisionAttempted = true;
                return;
            }

            if (!HunterPetProvisioningPolicy::ShouldProvision(true, bot->GetLevel(),
                bot->IsAlive(), bot->IsInCombat(), hasActivePet,
                hasSavedHunterPet, provisionAttempted))
                return;

            // A failed native creation is deterministic for this creature
            // template. Avoid allocating and logging again every maintenance pass.
            provisionAttempted = true;
            Pet* pet = bot->CreateTamedPetFrom(
                HunterPetProvisioningPolicy::DefaultCreatureEntry);
            if (!pet)
            {
                TC_LOG_ERROR("server", "[WorldBots] [Maintenance] Could not provision the default Hunter pet (Creature {}) for bot '{}' (GUID {}).",
                    HunterPetProvisioningPolicy::DefaultCreatureEntry, bot->GetName(),
                    bot->GetGUID().GetCounter());
                return;
            }

            pet->GetMap()->AddToMap(pet->ToCreature());
            bot->SetMinion(pet, true);
            pet->InitTalentForLevel();
            pet->SavePetToDB(PET_SAVE_AS_CURRENT);
            bot->PetSpellInitialize();

            TC_LOG_INFO("server", "[WorldBots] [Maintenance] Provisioned Hunter bot '{}' (GUID {}, Level {}) with persistent pet Creature {}.",
                bot->GetName(), bot->GetGUID().GetCounter(), bot->GetLevel(),
                HunterPetProvisioningPolicy::DefaultCreatureEntry);
        }

        PetStable::PetInfo const* GetSavedHunterPet(Player* bot)
        {
            if (!bot)
                return nullptr;

            PetStable& stable = bot->GetOrInitPetStable();
            if (stable.CurrentPet && stable.CurrentPet->Type == HUNTER_PET)
                return &stable.CurrentPet.value();
            return stable.GetUnslottedHunterPet();
        }

        void ConfigureActiveHunterPet(Player* bot, Pet* pet)
        {
            if (!bot || !pet || pet->getPetType() != HUNTER_PET)
                return;

            // Managed Hunters do not carry food. Keeping happiness capped gives
            // their pets the intended happy-state damage bonus without an
            // inventory/feeding simulation.
            uint32_t maxHappiness = pet->GetMaxPower(POWER_HAPPINESS);
            if (pet->GetPower(POWER_HAPPINESS) < maxHappiness)
            {
                pet->SetPower(POWER_HAPPINESS, maxHappiness);
            }

            // Defensive lets the pet protect its Hunter without independently
            // pulling nearby packs. EngagePet still explicitly assigns targets.
            if (pet->GetReactState() != REACT_DEFENSIVE)
                pet->SetReactState(REACT_DEFENSIVE);

            bool actionBarChanged = false;
            for (auto& [spellId, petSpell] : pet->m_spells)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (!spellInfo || !spellInfo->IsAutocastable())
                    continue;

                bool isCower = sSpellMgr->GetFirstSpellInChain(spellId) == CowerSpell;
                bool enable = HunterPetManagementPolicy::ShouldEnableAutocast(true, isCower);
                ActiveStates desired = enable ? ACT_ENABLED : ACT_DISABLED;
                if (petSpell.active == desired)
                    continue;

                pet->ToggleAutocast(spellInfo, enable);
                if (CharmInfo* charmInfo = pet->GetCharmInfo())
                    charmInfo->SetSpellAutocast(spellInfo, enable);
                actionBarChanged = true;
            }

            if (actionBarChanged)
                bot->PetSpellInitialize();
        }

        void RecoverAndMaintainHunterPet(Player* bot)
        {
            bool eligibleOwner = HunterPetManagementPolicy::IsEligibleOwner(
                bot && bot->GetClass() == CLASS_HUNTER,
                bot ? bot->GetLevel() : 0,
                bot && bot->IsAlive());
            if (!eligibleOwner)
                return;

            Pet* pet = bot->GetPet();
            PetStable::PetInfo const* savedPet = GetSavedHunterPet(bot);
            HunterPetManagementPolicy::RecoveryAction recovery =
                HunterPetManagementPolicy::ChooseRecoveryAction(
                    eligibleOwner, bot->IsInCombat(),
                    bot->HasUnitState(UNIT_STATE_CASTING), bot->IsStopped(),
                    pet != nullptr, pet && pet->IsAlive(), savedPet != nullptr,
                    savedPet && savedPet->Health > 0);

            uint32_t recoverySpell = recovery == HunterPetManagementPolicy::RecoveryAction::Call
                ? CallPetSpell
                : recovery == HunterPetManagementPolicy::RecoveryAction::Revive
                    ? RevivePetSpell : 0;
            if (recoverySpell != 0)
            {
                uint32_t readyRank = SpellUtils::FindReadyRank(bot, recoverySpell);
                if (readyRank != 0)
                    SpellUtils::TryCast(bot, bot, readyRank);
            }

            // Apply pet state after provisioning/load/revival. Mend is also
            // allowed while resting; combat Mend Pet is prioritized by the
            // Hunter strategy itself.
            pet = bot->GetPet();
            if (!pet || !pet->IsAlive())
                return;

            ConfigureActiveHunterPet(bot, pet);
            bool shouldMend = HunterPetManagementPolicy::ShouldMend(
                eligibleOwner, true,
                pet->HealthBelowPct(HunterPetManagementPolicy::MendHealthPct),
                SpellUtils::HasAuraInChain(pet, MendPetSpell, bot->GetGUID()));
            if (!bot->IsInCombat() && shouldMend)
            {
                uint32_t readyRank = SpellUtils::FindReadyRank(bot, MendPetSpell);
                if (readyRank != 0)
                    SpellUtils::TryCast(bot, pet, readyRank);
            }
        }
    }

    void BotMaintenance::Update(Player* bot, uint8_t& lastLearnedLevel,
        uint8_t& lastProgressionLevel, bool& hunterPetProvisionAttempted)
    {
        if (!bot || !bot->IsInWorld())
            return;

        SpellLearningUtils::AutoLearnClassSpells(bot, lastLearnedLevel);
        ProgressionUtils::MaintainCharacter(bot, lastProgressionLevel);
        ProgressionUtils::EnsureFactionFlightPathsLearned(bot);
        ProvisionHunterPet(bot, hunterPetProvisionAttempted);
        RecoverAndMaintainHunterPet(bot);
    }
}
