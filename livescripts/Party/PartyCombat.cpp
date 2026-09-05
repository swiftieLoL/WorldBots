#include "PartyCombat.h"

#include "Blackboard/BotBlackboard.h"
#include "Combat/ClassStrategies/ClassStrategyUtils.h"
#include "Helper/SpellUtils.h"
#include "ObjectAccessor.h"

namespace Party
{
    namespace
    {
        bool TryHeal(Player* bot, Player* member, MovementManager* movement)
        {
            if (!bot || !member || !member->IsAlive() || member->GetMap() != bot->GetMap())
                return false;
            if (bot->IsNonMeleeSpellCast(false))
                return true;

            float distance = bot->GetDistance(member);
            if (distance > 40.0f)
                return false;
            if (distance > 30.0f || !bot->IsWithinLOSInMap(member))
            {
                if (movement)
                    movement->MoveTo(member->GetPositionX(), member->GetPositionY(),
                        member->GetPositionZ(), BotMovementState::Moving, false);
                return true;
            }

            bool cast = Combat::ClassStrategyUtils::TryCastEmergencyHeal(bot, member);
            if (cast && movement)
                movement->Stop();
            return cast;
        }

        bool TryTaunt(Player* bot, Unit* target)
        {
            if (!bot || !target || !target->IsAlive() || bot->GetDistance(target) > 30.0f ||
                !bot->IsWithinLOSInMap(target))
                return false;
            Unit* victim = target->GetVictim();
            if (!victim || victim == bot || !victim->GetGUID().IsPlayer())
                return false;

            switch (bot->GetClass())
            {
                case CLASS_WARRIOR:
                    return Combat::ClassStrategyUtils::TryCastRank(bot, target, 355, "Warrior", "Taunt");
                case CLASS_PALADIN:
                    return Combat::ClassStrategyUtils::TryCastRank(bot, target, 62124, "Paladin", "Hand of Reckoning");
                case CLASS_DEATH_KNIGHT:
                    return Combat::ClassStrategyUtils::TryCastRank(bot, target, 56222, "Death Knight", "Dark Command");
                case CLASS_DRUID:
                    if (bot->GetShapeshiftForm() != FORM_BEAR && bot->GetShapeshiftForm() != FORM_DIREBEAR &&
                        Combat::ClassStrategyUtils::TryCastRank(bot, bot, 5487, "Druid", "Bear Form"))
                        return true;
                    return Combat::ClassStrategyUtils::TryCastRank(bot, target, 6795, "Druid", "Growl");
                default:
                    return false;
            }
        }
    }

    bool HandleRoleAction(Player* bot, Unit* hostileTarget, MovementManager* movement,
        const Blackboard::BotBlackboard& blackboard)
    {
        if (!bot || !blackboard.party.isInGroup)
            return false;

        if (blackboard.party.role == Role::Healer &&
            blackboard.party.lowestHealthGroupMemberGuid &&
            blackboard.party.lowestHealthGroupMemberPct < 65)
        {
            Player* member = ObjectAccessor::FindPlayer(blackboard.party.lowestHealthGroupMemberGuid);
            if (TryHeal(bot, member, movement))
                return true;
        }

        return blackboard.party.role == Role::Tank && TryTaunt(bot, hostileTarget);
    }
}
