import { BotFactoryController } from "./bot_factory";

export function Main(events: TSEvents) {
    BotFactoryController.Initialize();

    events.Player.OnCommand((player, command, found) => {
        const response = BotFactoryController.HandleCommand(command.get());
        if (response !== "") {
            player.SendBroadcastMessage(response);
            found.set(true);
        }
    });

    events.World.OnUpdate((diff) => {
        BotFactoryController.Update(diff);
    });
}
