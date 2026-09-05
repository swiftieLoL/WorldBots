#pragma once

#include "BotAction.h"
#include <string>
#include <utility>

namespace Actions
{
    class BaseBotAction : public BotAction
    {
    public:
        ~BaseBotAction() override = default;

        bool IsComplete() const override { return _completed; }
        ActionOutcome GetOutcome() const override { return _outcome; }
        FailureCategory GetFailureCategory() const override { return _failureCategory; }
        RecoveryDirective GetRecoveryDirective() const override { return _recoveryDirective; }
        const std::string& GetOutcomeReason() const override { return _outcomeReason; }
        void Abort(std::string reason, FailureCategory category,
            RecoveryDirective directive) override
        {
            Finish(ActionOutcome::RetryableFailure, std::move(reason), category,
                directive);
        }

    protected:
        void ResetOutcome()
        {
            _completed = false;
            _outcome = ActionOutcome::Running;
            _failureCategory = FailureCategory::None;
            _recoveryDirective = RecoveryDirective::None;
            _outcomeReason.clear();
        }

        void Finish(ActionOutcome outcome, std::string reason = "",
            FailureCategory category = FailureCategory::None,
            RecoveryDirective directive = RecoveryDirective::None)
        {
            _completed = true;
            _outcome = outcome;
            _outcomeReason = std::move(reason);
            _failureCategory = category;
            _recoveryDirective = directive;
        }

        void SetFailure(ActionOutcome outcome, std::string reason,
            FailureCategory category, RecoveryDirective directive)
        {
            Finish(outcome, std::move(reason), category, directive);
        }

        bool _completed = false;
        ActionOutcome _outcome = ActionOutcome::Running;
        FailureCategory _failureCategory = FailureCategory::None;
        RecoveryDirective _recoveryDirective = RecoveryDirective::None;
        std::string _outcomeReason;
    };
}
