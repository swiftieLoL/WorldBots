#pragma once

#include "BaseBotAction.h"
#include <cstdint>

namespace Actions
{
    class UnstuckAction : public BaseBotAction
    {
    public:
        UnstuckAction(uint32_t deadlyQuestId = 0,
            bool progressionRecovery = false);

        const char* GetName() const override { return "UnstuckAction"; }

        void Start(Player* bot, MovementManager* movement) override;
        void Update(Player* bot, MovementManager* movement, const Blackboard::BotBlackboard& blackboard, uint32_t deltaMs) override;
        void Stop(Player* bot, MovementManager* movement) override;
        bool IsProgressionRecovery() const { return _progressionRecovery; }
        bool DidMateriallyChangeEcology() const
        {
            return _materialEcologyChange;
        }
        // XP or a level-up changes the viable level band and proves the most
        // recent ecology useful. Reconsider every previously rejected hub.
        static void RecordProgress(ObjectGuid botGuid);
        // A new, rate-limited progression epoch must be allowed to reconsider
        // hubs rejected by the exhausted epoch; otherwise the candidate set
        // shrinks permanently until logout.
        static void ResetRecoveryCandidates(ObjectGuid botGuid);
        static void ClearBotState(ObjectGuid botGuid);
        static void ClearAllState();

    private:
        uint32_t _deadlyQuestId;
        bool _progressionRecovery = false;
        bool _materialEcologyChange = false;
    };
}
