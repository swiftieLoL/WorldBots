#include "BotCommands.h"
#include "Core/CoreLogic.h"
#include "Brain/BotBrain.h"
#include "Config/BotConfig.h"
#include "Diagnostics/BotTrace.h"
#include "Factory/BotFactory.h"
#include "Testing/ScenarioRunner.h"
#include "Player.h"
#include <string_view>

namespace Commands
{
    namespace
    {
        std::string_view TrimLeft(std::string_view sv)
        {
            while (!sv.empty() && sv.front() == ' ')
                sv.remove_prefix(1);
            return sv;
        }

        std::string_view TrimRight(std::string_view sv)
        {
            while (!sv.empty() && sv.back() == ' ')
                sv.remove_suffix(1);
            return sv;
        }

        std::string_view Trim(std::string_view sv)
        {
            return TrimRight(TrimLeft(sv));
        }
    }

    std::string BotCommands::Handle(std::string const& command)
    {
        std::string_view input = command;

        while (!input.empty() && (input.front() == ' ' || input.front() == '.'))
            input.remove_prefix(1);
        input = TrimRight(input);

        constexpr std::string_view testPrefix = "bot test";
        if (input.size() >= testPrefix.size() && input.substr(0, testPrefix.size()) == testPrefix &&
            (input.size() == testPrefix.size() || input[testPrefix.size()] == ' '))
        {
            input.remove_prefix(testPrefix.size());
            return Core::CoreLogic::RunTestCommand(std::string(input));
        }

        constexpr std::string_view tracePrefix = "bot trace";
        if (input.size() >= tracePrefix.size() && input.substr(0, tracePrefix.size()) == tracePrefix &&
            (input.size() == tracePrefix.size() || input[tracePrefix.size()] == ' '))
        {
            input.remove_prefix(tracePrefix.size());
            return Core::CoreLogic::RunTraceCommand(std::string(input));
        }

        constexpr std::string_view statusPrefix = "bot status";
        if (input == "bot")
            input = statusPrefix;

        if (input.size() < statusPrefix.size() || input.substr(0, statusPrefix.size()) != statusPrefix)
            return {};
        if (input.size() > statusPrefix.size() && input[statusPrefix.size()] != ' ')
            return {};

        input = TrimLeft(input.substr(statusPrefix.size()));

        constexpr std::string_view vendorMode = "vendor";
        if (input.size() >= vendorMode.size() && input.substr(0, vendorMode.size()) == vendorMode &&
            (input.size() == vendorMode.size() || input[vendorMode.size()] == ' '))
        {
            input = TrimLeft(input.substr(vendorMode.size()));
            return Core::CoreLogic::GetBotVendorStatus(input.empty() ? "Botharry" : std::string(input));
        }

        if (input == "factory" || input == "factory status")
            return Core::CoreLogic::GetFactoryStatus();

        constexpr std::string_view questMode = "quest";
        if (input.size() >= questMode.size() && input.substr(0, questMode.size()) == questMode &&
            (input.size() == questMode.size() || input[questMode.size()] == ' '))
        {
            input = TrimLeft(input.substr(questMode.size()));
            return Core::CoreLogic::GetBotQuestStatus(input.empty() ? "Botharry" : std::string(input));
        }

        return Core::CoreLogic::GetBotStatus(input.empty() ? "Botharry" : std::string(input));
    }

    std::string BotCommands::RunTest(std::string const& arguments, BrainResolver const& resolveBrain)
    {
        if (!Config::BotConfig::AreTestsEnabled())
            return "WorldBots tests are disabled. Set WorldBots.Tests.Enable = 1 to use .bot test commands.";

        std::string_view command = Trim(arguments);

        if (command.empty() || command == "list")
            return Testing::ScenarioRunner::ListScenarios();
        if (command == "logic")
            return Testing::ScenarioRunner::RunLogicScenarios();

        constexpr std::string_view planPrefix = "plan";
        if (command.size() >= planPrefix.size() && command.compare(0, planPrefix.size(), planPrefix) == 0 &&
            (command.size() == planPrefix.size() || command[planPrefix.size()] == ' '))
        {
            std::string_view botNameView = command.size() == planPrefix.size()
                ? std::string_view("Botharry") : TrimLeft(command.substr(planPrefix.size() + 1));
            std::string botName = botNameView.empty() ? "Botharry" : std::string(botNameView);

            Brain::BotBrain* brain = resolveBrain(botName);
            if (!brain)
                return "Bot '" + botName + "' has no active brain or is not in world.";
            return Testing::ScenarioRunner::DescribeTownPlan(
                Factory::BotFactory::NormalizeBotName(botName), brain->PreviewTownPlan());
        }

        return Testing::ScenarioRunner::ListScenarios();
    }

    std::string BotCommands::RunTrace(std::string const& arguments, BrainResolver const& resolveBrain)
    {
        if (!Config::BotConfig::AreTestsEnabled())
            return "WorldBots trace commands are disabled. Set WorldBots.Tests.Enable = 1 to use .bot trace.";

        std::string_view command = Trim(arguments);
        std::size_t separator = command.find(' ');
        std::string botName = std::string(separator == std::string_view::npos ? command : command.substr(0, separator));
        std::string mode = std::string(separator == std::string_view::npos ? std::string_view("status") : TrimLeft(command.substr(separator + 1)));

        if (botName.empty() || (mode != "on" && mode != "only" &&
            mode != "off" && mode != "status"))
            return "Usage: .bot trace <name> on|only|off|status";

        Brain::BotBrain* brain = resolveBrain(botName);
        Player* bot = brain ? brain->GetBot() : nullptr;
        if (!bot)
            return "Bot '" + botName + "' has no active brain or is not in world.";

        uint32_t guidLow = static_cast<uint32_t>(bot->GetGUID().GetCounter());
        if (mode == "only")
        {
            Diagnostics::BotTrace::Clear();
            Diagnostics::BotTrace::SetEnabled(guidLow, true);
        }
        else if (mode == "on")
            Diagnostics::BotTrace::SetEnabled(guidLow, true);
        else if (mode == "off")
            Diagnostics::BotTrace::SetEnabled(guidLow, false);

        bool enabled = Diagnostics::BotTrace::IsEnabled(guidLow);
        std::string formattedName = Factory::BotFactory::NormalizeBotName(botName);
        std::string response = "Trace for bot '" + formattedName + "' is " +
            (enabled ? "enabled." : "disabled.");
        if (mode == "only")
            response += " All other per-bot traces were disabled.";
        response += " Logging mode is ";
        response += Diagnostics::BotTrace::GetModeName();
        response += ".";
        if (Diagnostics::BotTrace::IsGlobalVerbose())
            response += " Verbose mode also emits detail for every other bot.";
        return response;
    }
}
