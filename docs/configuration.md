# WorldBots configuration reference

WorldBots loads its runtime settings from the module-local [worldbots.conf](../config/worldbots.conf). No realm or TrinityCore configuration file outside the WorldBots folder needs to be changed.

The file must retain its `[worldserver]` header because WorldBots merges that section into TrinityCore's in-memory `ConfigMgr` during module startup. Restart the realm after changing it.

Do not put `WorldBots.*` settings in [livescripts.conf](../livescripts/livescripts.conf). TSWoW's build tooling consumes that file; it only selects the native LiveScripts backend. The active runtime file is:

```text
E:\TSFresh\tswow-install\modules\WorldBots\config\worldbots.conf
```

Use [worldbots.conf.example](../config/worldbots.conf.example) as a safe template if the active file needs to be reset. The example keeps test commands disabled; the active development file currently enables them.

The module build embeds the absolute path of this module-local file. If the WorldBots folder is moved, rebuild the module once so it records the new location. Ordinary setting changes do not require recompilation, only a realm restart.

## Recommended development-test configuration

```ini
[worldserver]

WorldBots.Enable = 1
WorldBots.BotCount = 1
WorldBots.MaxBotCount = 2000
WorldBots.DebugMode = 1
WorldBots.VerboseLogging = 0
WorldBots.Tests.Enable = 1
WorldBots.AccountMode = dedicated
WorldBots.AccountPrefix = WBOT
WorldBots.AutoCreateAccounts = 1
WorldBots.MigrateLegacyCharacters = 1
WorldBots.AccountId = 1

# Optional while running disposable scenarios.
WorldBots.SaveBotProgress = 0
WorldBots.BaseSpawnDelayMs = 1000
WorldBots.SpawnDelayStepMs = 250
WorldBots.FactoryOpsPerTick = 1
WorldBots.MaxConcurrentLogins = 8
WorldBots.LoginTimeoutMs = 120000
WorldBots.LoginMaxRetries = 5
WorldBots.LoginRetryInitialDelayMs = 5000
WorldBots.LoginRetryMaxDelayMs = 60000
WorldBots.SaveBatchSize = 10
WorldBots.SaveBatchIntervalMs = 500
WorldBots.SaveBotIntervalMs = 30000
WorldBots.PersistentBotPercent = 100
WorldBots.StarterBagItemId = 41599
WorldBots.StarterBagCount = 4
WorldBots.GrindFallback.Enable = 1
WorldBots.QuestMaxLevelsAboveBot = 1
WorldBots.GrindMinLevelOffset = -3
WorldBots.GrindMaxLevelOffset = 0
WorldBots.DefaultRace = 1
WorldBots.DefaultClass = 1
WorldBots.DefaultGender = 0
WorldBots.Roster =
```

Do not use `WorldBots.SaveBotProgress = 0` if test-bot progress should survive restarts.

## Runtime flags

| Setting | Default | Purpose | Test guidance |
| --- | ---: | --- | --- |
| `WorldBots.Enable` | `1` | Starts or stops the WorldBots runtime. | Must be `1` for live bots, `.bot status`, and `.bot test plan`. |
| `WorldBots.BotCount` | `3` | Number of bots initialized by the factory. Negative values are clamped to zero by configuration parsing. | Use `1` for focused testing and a larger value for concurrency tests. |
| `WorldBots.MaxBotCount` | `2000` | Safety ceiling applied to `BotCount`. | Keep factory preparation rate-limited for large stress runs. |
| `WorldBots.DebugMode` | `1` | Enables debug-mode behavior. Periodic position output requires debug mode plus either global verbose logging or an explicit per-bot trace. | Keep enabled when traced position output is useful. |
| `WorldBots.VerboseLogging` | `0` | Globally enables detailed informational logs for every bot. It overrides per-bot trace selection. | Leave at `0` for focused testing and use `.bot trace <name> on`. |
| `WorldBots.Tests.Enable` | `0` | Enables `.bot test` and `.bot trace` development commands. | Enable only on a development realm. Command access is not separately permission-gated by WorldBots. |
| `WorldBots.AccountMode` | `dedicated` | Uses one isolated account per bot. Set to `shared` only to restore legacy behavior. | Keep `dedicated` for concurrency and account safety. |
| `WorldBots.AccountPrefix` | `WBOT` | Reserved prefix for automatically managed accounts. | Changing it creates a separate account pool. |
| `WorldBots.AutoCreateAccounts` | `1` | Creates missing dedicated accounts with random internal passwords. | Recommended; these accounts are marked and collision-checked before use. |
| `WorldBots.MigrateLegacyCharacters` | `1` | Moves generated bot characters from the legacy `AccountId` into their isolated accounts without resetting progress. | Disable before first restart if migration is not desired. |
| `WorldBots.AccountId` | `1` | Legacy shared-account ID and the only non-dedicated source authorized for migration. | It is no longer the destination in dedicated mode. Set it to the account that currently owns old Botharry characters. |
| `WorldBots.SaveBotProgress` | `1` | Periodically saves active bots to the character database. | Set to `0` for disposable tests; keep `1` for progression testing that must persist. |
| `WorldBots.PersistentBotPercent` | `100` | Percentage of slots reused across restarts when saving is enabled; remaining slots are recreated. | Use `100` for progression and a lower value for mixed lifecycle testing. |
| `WorldBots.StarterBagItemId` | `41599` | Container equipped into empty managed-bot bag slots. The default is the 20-slot Frostweave Bag. | Set to `0` to disable bag provisioning. Existing populated bags are never replaced. |
| `WorldBots.StarterBagCount` | `4` | Maximum number of empty equipped bag slots filled for a bot. | Values above four are clamped to four; set to `0` to disable. |
| `WorldBots.GrindFallback.Enable` | `1` | Enables active level grinding when no suitable quest can run or a quest enters progression recovery. | Disable to restore the older wander-only fallback. |
| `WorldBots.QuestMaxLevelsAboveBot` | `1` | Highest quest level relative to the bot that normal solo quest selection may choose. | The default permits equal-level and +1 quests; higher quests wait. |
| `WorldBots.GrindMinLevelOffset` | `-3` | Lowest creature level, relative to the bot, selected for deliberate grinding. | Clamped to `-10..0`. |
| `WorldBots.GrindMaxLevelOffset` | `0` | Highest creature level, relative to the bot, selected for deliberate grinding. | Default avoids deliberately pulling above-level enemies; clamped to `-5..3`. |
| `WorldBots.BaseSpawnDelayMs` | `1000` | Initial delay used by staggered bot spawning, in milliseconds. | Increase if login or database load is under investigation. |
| `WorldBots.SpawnDelayStepMs` | `250` | Additional stagger between bot spawns, in milliseconds. | Increase for large bot-count stress tests if simultaneous logins are noisy. |
| `WorldBots.FactoryOpsPerTick` | `1` | Maximum account/character preparations performed per 100ms factory tick. | Increase gradually while watching database latency. |
| `WorldBots.MaxConcurrentLogins` | `8` | Maximum asynchronous character login pipelines in flight at once. | Keep bounded for large populations; raise only after measuring database headroom. |
| `WorldBots.LoginTimeoutMs` | `120000` | Time allowed for a login pipeline before it is cancelled and retried. Minimum 10 seconds. | The longer default tolerates a busy character database without abandoning bots. |
| `WorldBots.LoginMaxRetries` | `5` | Retries after the initial login attempt. | Set to `0` to disable retries. Failures are logged after the final attempt. |
| `WorldBots.LoginRetryInitialDelayMs` | `5000` | Delay before the first retry. | Later retry delays double automatically. |
| `WorldBots.LoginRetryMaxDelayMs` | `60000` | Maximum retry delay. | Caps exponential backoff during extended database pressure. |
| `WorldBots.SaveBatchSize` | `10` | Maximum active bots persisted during one save task execution. | Lower it if saves cause visible world-tick spikes. |
| `WorldBots.SaveBatchIntervalMs` | `500` | Interval between save batches. Minimum 100 ms. | Together with batch size, controls persistence throughput. |
| `WorldBots.SaveBotIntervalMs` | `30000` | Minimum target interval between saves of the same bot. Minimum 5 seconds. | At very large populations the full round-robin may take longer by design. |
| `WorldBots.DefaultRace` | `1` | Race ID used when creating a managed bot. `1` is Human. | Choose a race compatible with the selected class. |
| `WorldBots.DefaultClass` | `1` | Class ID used when creating a managed bot. `1` is Warrior. | Use supported class/race combinations. |
| `WorldBots.DefaultGender` | `0` | Gender used when creating a managed bot. `0` is Male. | Only affects newly created bots. |
| `WorldBots.Roster` | empty | Optional repeating `race:class:gender:profile` pattern. Profiles are `balanced`, `cautious`, `questing`, and `stress`. | Example: `1:1:0:cautious,1:8:1:questing`. Invalid entries are logged and ignored. |

## Build-time test option

`WORLDBOTS_BUILD_LOGIC_TESTS` is a CMake option rather than a realm configuration flag.

| Option | Default | Purpose |
| --- | ---: | --- |
| `WORLDBOTS_BUILD_LOGIC_TESTS` | `ON` | Makes the `WorldBotsLogicTests` executable target available. |

The target is marked `EXCLUDE_FROM_ALL`, so building `WorldBots.dll` alone does not automatically build the standalone test executable. Build it explicitly:

```powershell
& 'E:\TSFresh\tswow-install\bin\cmake\bin\cmake.exe' `
    --build 'E:\TSFresh\tswow-install\modules\WorldBots\livescripts\build\default.dataset\lib' `
    --target WorldBotsLogicTests `
    --config RelWithDebInfo
```

## Production-oriented configuration

```ini
[worldserver]

WorldBots.Enable = 1
WorldBots.BotCount = 3
WorldBots.DebugMode = 0
WorldBots.VerboseLogging = 0
WorldBots.Tests.Enable = 0
WorldBots.SaveBotProgress = 1
WorldBots.AccountId = 1
```

The important production safeguard is `WorldBots.Tests.Enable = 0`. This prevents players from invoking the WorldBots test and trace command surface.

## Focused tracing configuration

```ini
WorldBots.DebugMode = 1
WorldBots.VerboseLogging = 0
WorldBots.Tests.Enable = 1
```

After the bot has logged in:

```text
.bot trace Botharry on
.bot trace Botharry status
.bot trace Botharry off
```

Trace selection is runtime-only and is not stored in the configuration or character database. Warnings and errors from untraced bots remain visible.

## LiveScripts build setting

`LiveScripts.Backend = "c++"` remains in [livescripts.conf](../livescripts/livescripts.conf). It selects the native LiveScripts compiler but does not contain WorldBots runtime settings. Those settings remain in [worldbots.conf](../config/worldbots.conf), inside this module.
