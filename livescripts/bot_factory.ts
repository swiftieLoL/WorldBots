import {
    BotFactorySpawn,
    BotFactorySpawnConfigured,
    BotFactoryUpdate,
    BotFactoryGetActiveCount,
    BotFactorySetVerboseLogging,
    BotHandleCommand,
    BotMoveTo,
    BotFollow,
    BotChase,
    BotStop,
    BotGetMovementState
} from "./Bindings/HelperBindings";

export enum BotMovementState {
    Idle = 0,
    Moving = 1,
    Following = 2,
    Chasing = 3,
    Fleeing = 4,
    Stuck = 5
}

export class BotFactoryController {
    private static initialized: boolean = false;

    public static Initialize(botCount: number = -1, debugMode: boolean = false, verboseLogging: boolean = false): boolean {
        if (this.initialized) {
            return true;
        }

        // Optional primitive parameters are emitted as literal `undefined`
        // defaults by this TSWoW transpiler, which is not valid C++. Use a
        // numeric sentinel so clean native builds remain reproducible.
        const configuredInitialization = botCount < 0;
        const started = configuredInitialization
            ? BotFactorySpawnConfigured()
            : (() => {
                BotFactorySpawn(botCount, debugMode, verboseLogging);
                return true;
            })();

        this.initialized = started;
        return started;
    }

    public static SetVerboseLogging(enabled: boolean): void {
        BotFactorySetVerboseLogging(enabled);
    }

    public static Update(diff: number): void {
        BotFactoryUpdate(diff);
    }

    public static GetActiveCount(): number {
        return BotFactoryGetActiveCount();
    }

    public static HandleCommand(command: string): string {
        return BotHandleCommand(command);
    }

    public static MoveTo(botGuidLow: number, x: number, y: number, z: number): boolean {
        return BotMoveTo(botGuidLow, x, y, z);
    }

    public static Follow(botGuidLow: number, targetGuidLow: number, distance: number = 2.0, angle: number = 0.0): void {
        BotFollow(botGuidLow, targetGuidLow, distance, angle);
    }

    public static Chase(botGuidLow: number, targetGuidLow: number): void {
        BotChase(botGuidLow, targetGuidLow);
    }

    public static Stop(botGuidLow: number): void {
        BotStop(botGuidLow);
    }

    public static GetMovementState(botGuidLow: number): BotMovementState {
        return BotGetMovementState(botGuidLow);
    }
}
