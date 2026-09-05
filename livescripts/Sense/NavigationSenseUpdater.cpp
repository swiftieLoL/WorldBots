#include "SenseUpdaters.h"
#include "Helper/MovementManager.h"
#include "Player.h"

namespace Sense
{
    bool NavigationSenseUpdater::Update(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        (void)movement;
        return Detail::ServiceSubstate(bb.nav, deltaMs,
            [&]() { Refresh(bot, movement, bb.nav); });
    }

    void NavigationSenseUpdater::Refresh(Player* bot, MovementManager* movement, Blackboard::NavigationState& nav)
    {
        if (movement)
        {
            nav.movementState = static_cast<uint8_t>(movement->GetState());
            nav.hasActivePath = movement->HasPath();
            nav.isStuck = (movement->GetState() == BotMovementState::Stuck);
            nav.destinationX = movement->GetDestinationX();
            nav.destinationY = movement->GetDestinationY();
            nav.destinationZ = movement->GetDestinationZ();
        }
    }

}
