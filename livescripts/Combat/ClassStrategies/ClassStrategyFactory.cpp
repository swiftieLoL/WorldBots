#include "Globals/ObjectMgr.h"
#include "ClassStrategyFactory.h"
#include "BasicMeleeStrategy.h"
#include "WarriorStrategy.h"
#include "MageStrategy.h"
#include "PriestStrategy.h"
#include "HunterStrategy.h"
#include "RogueStrategy.h"
#include "PaladinStrategy.h"
#include "DeathKnightStrategy.h"
#include "WarlockStrategy.h"
#include "DruidStrategy.h"
#include "ShamanStrategy.h"

namespace Combat
{
    std::unique_ptr<IClassStrategy> ClassStrategyFactory::GetStrategyForClass(uint8_t clazz)
    {
        switch (clazz)
        {
            case CLASS_WARRIOR:
                return std::make_unique<WarriorStrategy>();

            case CLASS_ROGUE:
                return std::make_unique<RogueStrategy>();

            case CLASS_PALADIN:
                return std::make_unique<PaladinStrategy>();

            case CLASS_DEATH_KNIGHT:
                return std::make_unique<DeathKnightStrategy>();

            case CLASS_HUNTER:
                return std::make_unique<HunterStrategy>();

            case CLASS_WARLOCK:
                return std::make_unique<WarlockStrategy>();

            case CLASS_DRUID:
                return std::make_unique<DruidStrategy>();

            case CLASS_SHAMAN:
                return std::make_unique<ShamanStrategy>();

            case CLASS_MAGE:
                return std::make_unique<MageStrategy>();

            case CLASS_PRIEST:
                return std::make_unique<PriestStrategy>();

            default:
                return std::make_unique<BasicMeleeStrategy>();
        }
    }
}
