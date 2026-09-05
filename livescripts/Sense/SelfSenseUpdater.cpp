#include "SenseUpdaters.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "Unit.h"

namespace Sense
{
    bool SelfSenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        return Detail::ServiceSubstate(bb.self, deltaMs,
            [&]() { Refresh(bot, bb.self); });
    }

    void SelfSenseUpdater::Refresh(Player* bot, Blackboard::SelfState& self)
    {
        uint8 botClass = bot->GetClass();
        bool hasManaPool = (botClass != CLASS_WARRIOR && botClass != CLASS_ROGUE && botClass != CLASS_DEATH_KNIGHT);
        self.health = bot->GetHealth();
        self.maxHealth = bot->GetMaxHealth();
        if (hasManaPool)
        {
            self.mana = bot->GetPower(POWER_MANA);
            self.maxMana = bot->GetMaxPower(POWER_MANA);
            self.manaPct = (self.maxMana > 0) ? static_cast<uint8_t>((self.mana * 100) / self.maxMana) : 100;
        }
        else
        {
            self.mana = 100;
            self.maxMana = 100;
            self.manaPct = 100;
        }

        self.healthPct = (self.maxHealth > 0) ? static_cast<uint8_t>((self.health * 100) / self.maxHealth) : 0;

        self.level = bot->GetLevel();
        self.money = bot->GetMoney();
        self.x = bot->GetPositionX();
        self.y = bot->GetPositionY();
        self.z = bot->GetPositionZ();
        self.orientation = bot->GetOrientation();
        self.mapId = bot->GetMapId();
        self.areaId = bot->GetAreaId();
        self.zoneId = bot->GetZoneId();
        self.inCombat = bot->IsInCombat();
        self.isDead = !bot->IsAlive();
        self.isMounted = bot->IsMounted();
        self.isSwimming = bot->IsInWater() || bot->IsUnderWater();

        // Crowd-Control & Aura Perception
        self.isStunned = bot->HasUnitState(UNIT_STATE_STUNNED);
        self.isFeared = bot->HasUnitState(UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED);
        self.isSilenced = bot->HasAuraType(SPELL_AURA_MOD_SILENCE) ||
                          bot->HasAuraType(SPELL_AURA_MOD_PACIFY_SILENCE);
        self.isRooted = bot->HasUnitState(UNIT_STATE_ROOT);
        // Hard CC prevents all actions (Stun, Fear/Confuse, Charm, Pacify/Polymorph).
        // Root only prevents movement; Silence only prevents spellcasting.
        bool isCharmed = bot->HasUnitState(UNIT_STATE_CHARMED);
        bool isPacified = bot->HasAuraType(SPELL_AURA_MOD_PACIFY) ||
                          bot->HasAuraType(SPELL_AURA_MOD_PACIFY_SILENCE);
        self.isCCed = self.isStunned || self.isFeared || isCharmed || isPacified;
    }

}
