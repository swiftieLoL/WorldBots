/** @tswow-c++ */
export declare function BotFactorySpawn(botCount: number, debugMode: boolean, verboseLogging?: boolean): void;

/** @tswow-c++ */
export declare function BotFactorySpawnConfigured(): boolean;

/** @tswow-c++ */
export declare function BotFactoryUpdate(diff: number): void;

/** @tswow-c++ */
export declare function BotFactoryGetActiveCount(): number;

/** @tswow-c++ */
export declare function BotFactorySetVerboseLogging(enabled: boolean): void;

/** @tswow-c++ */
export declare function BotHandleCommand(command: string): string;

/** @tswow-c++ */
export declare function BotMoveTo(botGuidLow: number, x: number, y: number, z: number): boolean;

/** @tswow-c++ */
export declare function BotFollow(botGuidLow: number, targetGuidLow: number, distance?: number, angle?: number): void;

/** @tswow-c++ */
export declare function BotChase(botGuidLow: number, targetGuidLow: number): void;

/** @tswow-c++ */
export declare function BotStop(botGuidLow: number): void;

/** @tswow-c++ */
export declare function BotGetMovementState(botGuidLow: number): number;
