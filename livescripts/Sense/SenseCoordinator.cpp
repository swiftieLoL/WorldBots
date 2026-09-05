#include "SenseCoordinator.h"
#include "SenseUpdaters.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include <array>

namespace Sense
{
    namespace
    {
        const std::array<ISenseUpdater*, 7>& GetUpdaters()
        {
            static SelfSenseUpdater self;
            static SpatialSenseUpdater spatial;
            static PartySenseUpdater party;
            static CombatSenseUpdater combat;
            static NavigationSenseUpdater navigation;
            static InventorySenseUpdater inventory;
            static QuestSenseUpdater quest;
            static const std::array<ISenseUpdater*, 7> updaters{
                &self, &spatial, &party, &combat, &navigation, &inventory, &quest
            };
            return updaters;
        }
    }

    void SenseCoordinator::ClearSharedCaches()
    {
        PartySenseUpdater::ClearSharedCache();
    }

    void SenseCoordinator::UpdateAll(Player* bot, MovementManager* movement,
        Blackboard::BotBlackboard& bb, uint32_t deltaMs)
    {
        if (!bot || !bot->IsInWorld())
            return;

        bool updated = false;
        for (ISenseUpdater* updater : GetUpdaters())
            updated = updater->Update(bot, movement, bb, deltaMs) || updated;

        if (updated)
            ++bb.generation;
        bb.initialSnapshotReady = bb.self.initialized && bb.spatial.initialized &&
            bb.party.initialized && bb.combat.initialized && bb.nav.initialized &&
            bb.inv.initialized && bb.quest.initialized;
    }
}

