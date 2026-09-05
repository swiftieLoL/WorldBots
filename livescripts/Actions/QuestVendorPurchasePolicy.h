#pragma once

#include "Actions/ActionTypes.h"

#include <algorithm>
#include <cstdint>

namespace Actions
{
    enum class QuestVendorPurchaseIssue : uint8_t
    {
        InsufficientMoney,
        OutOfStock,
        UnmetVendorCondition,
        ReputationRequirement,
        ExtendedCostRequirement,
        IneligibleCharacter,
        InvalidVendorData,
        UnexpectedRejection
    };

    struct QuestVendorPurchaseDecision
    {
        ActionOutcome outcome = ActionOutcome::RetryableFailure;
        FailureCategory category = FailureCategory::Transient;
        RecoveryDirective directive = RecoveryDirective::RetryLater;
        uint32_t retryDelaySeconds = 60;
    };

    inline QuestVendorPurchaseDecision SelectQuestVendorPurchaseRecovery(
        QuestVendorPurchaseIssue issue, uint32_t vendorRestockSeconds = 0)
    {
        switch (issue)
        {
            case QuestVendorPurchaseIssue::InsufficientMoney:
                // Leave the quest in the log, but give other quests or grind
                // fallback time to earn the missing copper.
                return { ActionOutcome::RetryableFailure,
                    FailureCategory::Transient,
                    RecoveryDirective::RetryLater, 120 };
            case QuestVendorPurchaseIssue::OutOfStock:
                return { ActionOutcome::RetryableFailure,
                    FailureCategory::Transient,
                    RecoveryDirective::RetryLater,
                    std::clamp<uint32_t>(vendorRestockSeconds, 60, 900) };
            case QuestVendorPurchaseIssue::UnmetVendorCondition:
            case QuestVendorPurchaseIssue::ReputationRequirement:
            case QuestVendorPurchaseIssue::ExtendedCostRequirement:
                // These prerequisites may change after other progression, but
                // gaining one character level is not itself the remedy.
                return { ActionOutcome::RetryableFailure,
                    FailureCategory::Transient,
                    RecoveryDirective::RetryLater, 900 };
            case QuestVendorPurchaseIssue::IneligibleCharacter:
            case QuestVendorPurchaseIssue::InvalidVendorData:
            case QuestVendorPurchaseIssue::UnexpectedRejection:
                return { ActionOutcome::Unsupported,
                    FailureCategory::ContentUnsupported,
                    RecoveryDirective::RetryLater, 0 };
        }

        return {};
    }
}
