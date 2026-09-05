#pragma once

namespace Helper::LiveEcologyPolicy
{
    struct CandidateState
    {
        bool alive = false;
        bool inWorld = false;
        bool samePhase = false;
        bool visibleAndAttackable = false;
        bool normalGrindCreature = false;
        bool levelSuitable = false;
        bool unclaimed = false;
        bool permittedByAction = false;
    };

    inline bool IsEligible(const CandidateState& state)
    {
        return state.alive && state.inWorld && state.samePhase &&
            state.visibleAndAttackable && state.normalGrindCreature &&
            state.levelSuitable && state.unclaimed && state.permittedByAction;
    }
}
