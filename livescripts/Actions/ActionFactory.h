#pragma once

#include "BotAction.h"
#include "Brain/ActionRequest.h"
#include <memory>

namespace Actions
{
    class ActionFactory
    {
    public:
        static std::unique_ptr<BotAction> CreateAction(const Brain::ActionRequest& request);
    };
}
