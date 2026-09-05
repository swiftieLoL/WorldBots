# Running the WorldBots tests

WorldBots has two complementary test paths:

1. `WorldBotsLogicTests.exe` tests the town-planning rules without starting a realm.
2. `.bot test` commands run through the loaded WorldBots module inside a development realm.

Per-bot tracing is also available for following one live bot without enabling verbose output for every bot.

The standalone suite is the fastest option and should be run after every planning change. The in-realm commands confirm that the rebuilt module is loaded and can also preview the plan produced from a live bot's blackboard.

## 1. Standalone logic tests

### What they cover

The executable currently checks:

- no town run when the bot has no town needs;
- selling, reward-space creation, and repair before quest turn-ins;
- nearest-first ordering of eligible turn-ins;
- no unnecessary vendor detour for incidental junk when bag space is healthy;
- missing-vendor handling;
- protected-inventory handling and full-bag cleanup when no conventional sellable item exists;
- rejection of cross-map or unknown turn-in locations;
- observable inventory progress after selling or capacity cleanup;
- observable durability progress after repairing.
- deterministic food/drink reserve selection for mana and non-mana bots.
- quest difficulty gating, the conservative grind level band, and retry-level calculation.
- ranged-band decisions for closing distance, holding position, and escaping a dead zone.
- party role assignment, formation ordering, shared-quest selection, and resurrection capability.
- stale-death pruning, bounded danger-area suppression, quest-hub failure
  escalation, and one-attempt Hearthstone fallback;
- scheduler catch-up elapsed values and safe task-list mutation during callbacks.

These tests do not require MySQL, a running world server, a logged-in bot, or `WorldBots.Tests.Enable`.

Class rotations require a live realm because spell availability, range,
resources, pets, equipment, and cooldowns are TrinityCore entity state. For a
focused combat pass, create or reuse one bot of each playable class, enable its
trace, and require the strategy name and a class ability—not
`BasicMeleeStrategy`—to appear during a same-level fight. Hunter should command
an existing pet, hold 8–30 yards when terrain permits, maintain a sting and
auto-shot, and switch to Wing Clip/Disengage or melee fallback inside its dead
zone. Warlock should command an existing pet and maintain its damage-over-time
effects. Healing hybrids should use a self-heal before resuming damage.

For a live progression check, trace one bot and run `.bot status quest <name>`.
The status includes the bot level, quest ceiling, selected and suspended
quests, and the level at which a failed quest becomes eligible again. A bot
with no actionable active quest should accept suitable nearby work first. If
the local area is exhausted, it should select a reachable, level-appropriate
world starter and use `AcceptQuestAction`/`WorldTravel` to migrate there.
`Grind / GrindAction` is valid only when active, nearby, and reachable remote
quest work are all unavailable, and it must still yield to rest, loot, town,
flee, and resurrection priorities.

Quest travel is preflighted at selection time. An unreachable first bounded
ground leg emits `quest_travel_preflight_rejected`; a completed travel failure
emits either `quest_destination_retry_deferred` or
`quest_destination_suppressed`. The second failure at the same destination hub
within ten minutes suppresses every quest giver within 80 yards for 15 minutes.
This suppression is per bot, is consumed by nearby and world-starter discovery,
and is cleared early when the bot successfully reaches that hub. Hearthstone
waits must not exceed 20 seconds for one attempt inside a journey; after that
failure, the route must replan without Hearthstone rather than repeat it.

### Build the executable

The generated native build directory must already exist. A normal TSWoW module build creates it.

From PowerShell:

From PowerShell or terminal:

```powershell
Set-Location '<build-dir>'

cmake --build . `
    --target WorldBotsLogicTests `
    --config RelWithDebInfo
```

The test target is controlled by the CMake option `WORLDBOTS_BUILD_LOGIC_TESTS`. It defaults to `ON`. If the target is unavailable, reconfigure the generated build with that option enabled or perform a normal TSWoW build so the updated module CMake file is included.

### Run the executable

```powershell
Set-Location '<build-dir>'
& '.\RelWithDebInfo\WorldBotsLogicTests.exe'
```

Successful output:

```text
All WorldBots logic tests passed.
```

A failed assertion is printed as `FAILED: <reason>` and the process exits with code `1`. A successful run exits with code `0`.

### Debug builds

Use the same configuration when building and running:

```powershell
cmake --build . `
    --target WorldBotsLogicTests `
    --config Debug

& '.\Debug\WorldBotsLogicTests.exe'
```

## 2. In-realm test commands

The in-realm commands are disabled by default. They are intended for a local development realm and are read-only: they do not add items, damage equipment, alter quests, or force the bot to begin a town run.

### Enable the commands

Open the module-local [worldbots.conf](../config/worldbots.conf) and set:

```ini
[worldserver]

WorldBots.Enable = 1
WorldBots.Tests.Enable = 1
```

The active file stays inside this module:

```text
<tswow-root>/modules/WorldBots/config/worldbots.conf
```

Keep the `[worldserver]` header and restart the realm after changing the file. No `worldserver.conf`, realm-module, or TrinityCore file outside WorldBots needs to be edited. [worldbots.conf.example](../config/worldbots.conf.example) is a safe reference template with tests disabled.

`LiveScripts.Backend = "c++"` remains separately in [livescripts.conf](../livescripts/livescripts.conf) because it is a TSWoW build setting, not the WorldBots runtime configuration.

`WorldBots.Tests.Enable` controls `.bot test` and `.bot trace` commands. It does not control the standalone executable.

### Available commands

Run these through the normal in-game chat command input:

```text
.bot test
.bot test list
.bot test logic
.bot test plan Botharry
.bot trace Botharry on
.bot trace Botharry only
.bot trace Botharry status
.bot trace Botharry off
.bot status Botharry
.bot status factory
.bot status vendor Botharry
.bot status quest Botharry
```

Command behavior:

| Command | Result |
| --- | --- |
| `.bot test` | Lists available WorldBots test commands. |
| `.bot test list` | Same as `.bot test`. |
| `.bot test logic` | Runs deterministic town-planner scenarios inside the loaded realm module. |
| `.bot test plan <name>` | Produces a read-only plan from the named live bot's current blackboard. |
| `.bot test plan` | Uses `Botharry` as the default bot name. |
| `.bot trace <name> on` | Enables detailed informational logs for one active bot. |
| `.bot trace <name> only` | Enables detailed logs for the named bot and disables all other per-bot traces. |
| `.bot trace <name> status` | Reports whether explicit tracing is enabled for that bot. |
| `.bot trace <name> off` | Disables explicit tracing for that bot. |
| `.bot status <name>` | Reports the bot's goal, action, movement, quest counts, and position. |
| `.bot status factory` | Reports active bots, account/character preparation backlog, delayed logins, account mode, and scale limits. |
| `.bot status vendor <name>` | Reports the town plan, vendor discovery and suppression, cleanup backoff, projected free slots, and the sell/protect decision for every bag stack. |
| `.bot status quest <name>` | Reports selected and suspended quests, objective progress and targets, quest-item store preflights, progress-watch time, and inventory blockage. |

All status commands use `Botharry` when the name is omitted. They return the snapshot in-game and write the same snapshot to the PowerShell realm console under `[WorldBots] [Status]`. The normal movement section reports the current movement state, whether a path is active, the external-control mode, and the current point-movement destination. External `MoveTo` control releases automatically when its finite route completes or fails; external Follow and Chase remain active until the controlling script calls `BotStop`.

During graveyard repop or another server teleport, `.bot status` may briefly report `Lifecycle: Temporarily unavailable during repop/teleport` with `Brain: Preserved`. This is a protected 30-second transition, not a logout. Normal status should return as soon as the server-side teleport acknowledgement completes. If it does not, WorldBots rebuilds the owned socketless session after the grace period and logs that recovery under `[WorldBots] [Lifecycle]`.

The vendor report uses the exact same classification function as `VendorAction`. `Cleanup Projection` therefore answers whether the current policy can create the requested number of slots. An inventory containing only `protected` decisions cannot be fixed by visiting a different vendor. `Cleanup Backoff` explains why the bot is not immediately repeating a failed visit.

For collection quests, the quest report includes a one-item `Store preflight`. `INVENTORY_FULL (50)` means the bot must create capacity before engaging that objective's source. Other inventory results indicate a restriction other than ordinary bag capacity.

Successful `.bot test logic` output ends with:

```text
Result: 8/8 passed
```

This confirms that the realm loaded a module containing the expected planner. It still does not exercise pathfinding, database contents, live vendor interaction, or actual quest rewards.

## Per-bot tracing

Use `important` logging with per-bot tracing:

```ini
WorldBots.Logging = important
WorldBots.Logging.Positions = 1
WorldBots.Tests.Enable = 1
```

Then enable the bot you want to follow:

```text
.bot trace Botharry only
.bot status Botharry
```

The trace covers recurring informational diagnostics such as:

- brain goal transitions;
- quest-item source resolution;
- quest travel and target engagement;
- combat spell choices;
- looting and rest activity;
- wandering destinations;
- vendor travel, inventory contents, selling, and free direct repairs at armorers;
- periodic positions when `WorldBots.Logging.Positions = 1`.

Warning severity alone does not bypass the WorldBots mode. In `important`, routine per-bot recovery warnings are hidden unless that bot is traced; module/runtime failures, deaths, and severe recovery events remain visible for the roster.

Trace state is held in memory. It is cleared when the bot runtime is removed, the WorldBots runtime is reinitialized, or the realm restarts. Enable it again after a restart.

`WorldBots.Logging = verbose` is a global override. In that mode all bots emit detailed diagnostics even if `.bot trace <name> off` is used. `normal` is useful when routine milestones are wanted for the whole roster without spell-by-spell and movement detail.

### Reading a plan preview

Example:

```text
[WorldBots Town Plan] Botharry
 - Target free slots: 5
 - Missing vendor: No
 - Protected inventory block: No
 - Steps:
   1. Sell
   2. CreateRewardSpace
   3. Repair
   4. TurnInQuest (Quest 123)
```

The fields mean:

- `Target free slots` is the capacity the vendor visit must produce before a blocked quest reward is attempted.
- `Missing vendor` means no live nearby service NPC or suitable stationary cached NPC offers all required services.
- `Protected inventory block` means the planner cannot prove that enough space can be created without touching protected items.
- `Steps` shows the order that `TownRunAction` would use. Vendor operations are combined into one visit when one NPC offers the necessary services.

`Steps: none` is valid when the bot currently has no town need. If that result is unexpected, check that the bot is in world and allow at least one blackboard refresh interval before running the preview again.

## Suggested live verification workflow

### Unattended progression soak diagnostics

Enable the four `WorldBots.Diagnostics.*` settings documented in
[configuration.md](configuration.md) before a long progression run. The realm
then writes two tab-separated files without requiring verbose console logging:

- `worldbots-live.tsv` contains one row per active bot, sorted by lowest level
  and longest time without XP first. It is replaced every interval and is the
  file to inspect or live-monitor.
- `worldbots-history-v5.tsv` appends the same rows every interval. Use it after a
  run to reconstruct goal, action, quest-watchdog, movement, inventory, death,
  and recovery changes leading up to a stall.

The `runtime_active` column makes provisioning, login, and lifecycle gaps
visible instead of silently omitting the bot. The `stalled` column becomes `1`
after the configured XP/level grace period.
The adjacent `suspected_reason` column classifies common states such as
`quest_no_progress`, `navigation_stuck`, `death_recovery`,
`grind_without_target_or_path`, and `inventory_full`. Classification is a
diagnostic lead rather than a replacement for the surrounding state columns.
While `GrindAction` is active, `action_detail` reports live candidate rejection
counts and cached-anchor search results, including level/rank filtering,
suppression, inactive events, faction mismatch, personal hazards, unsafe packs,
distance filtering, viable ecology cells, and path rejection.

1. Start the development realm with tests enabled.
2. Confirm the bot is active with `.bot status Botharry`.
3. Run `.bot test logic` and require `8/8 passed`.
4. Run `.bot trace Botharry on`.
5. Run `.bot test plan Botharry` before changing the bot's state.
6. Create one normal gameplay condition at a time: damaged equipment, low bag space, or a completed quest.
7. Run the plan preview again and verify that only the expected steps appear.
8. Let the normal brain choose `TownRunAction`; use `.bot status Botharry` to observe the current goal, action, and trace state.
9. Check the realm log for `[WorldBots] [Status]`, `[WorldBots] [Vendor]`, `[WorldBots] [Quest]`, and `[WorldBots] [Brain]` entries.
10. Run `.bot trace Botharry off` when finished.

For a patrolling vendor, keep the bot within 30 yards when the plan is evaluated. Moving vendors are intentionally discovered from their live position and are not used as long-range stationary cache destinations.

## Troubleshooting

### "WorldBots tests are disabled"

Set `WorldBots.Tests.Enable = 1` in [config/worldbots.conf](../config/worldbots.conf), then restart the realm. Do not place the flag in `livescripts/livescripts.conf`; that file is only for TSWoW's build backend.

After installing this loader change for the first time, rebuild and deploy WorldBots through the normal TSWoW module workflow, then restart the realm. Later edits to `config/worldbots.conf` only require a restart.

The same flag gates `.bot trace` commands.

### Other bots are still producing detailed logs

Set `WorldBots.Logging = important` and restart or reload the effective configuration. Then run `.bot trace <name> only` for the bot under investigation. Module/runtime failures, deaths, startup messages, and severe recovery events are intentionally not suppressed.

### A large run stops below the configured bot count

Run `.bot status factory`. `Login pipelines in flight` should remain at or below
`WorldBots.MaxConcurrentLogins`, while `Prepared login queue` drains over time.
Transient database timeouts are requeued automatically and logged with their
retry number. If retries are repeatedly exhausted, reduce concurrent logins or
increase the login timeout before increasing database load.

The generated bot-name sequence supports the full 2,000-bot default ceiling;
a plateau near 660 is not a name-pool limit. It indicates login/database
backpressure in older builds that launched every ready login without a bound.

### "Bot '<name>' has no active brain or is not in world"

Check spelling and run `.bot status <name>`. The plan preview requires an active runtime and a bot that has completed login.

### `WorldBotsLogicTests` is not a build target

The generated build may predate the test target, or `WORLDBOTS_BUILD_LOGIC_TESTS` may be disabled. Run the normal TSWoW build/configuration step, then build the target again.

### The executable cannot be found

Confirm that the build configuration in the executable path matches the one supplied to `--config`. For example, `RelWithDebInfo` produces:

```text
livescripts\build\default.dataset\lib\RelWithDebInfo\WorldBotsLogicTests.exe
```

### The plan reports a missing vendor

The chosen NPC must provide every service required by the combined visit. A repair-only NPC cannot satisfy a sell-and-repair plan. Stationary NPCs can be selected from the cache; patrolling vendors must currently be live and nearby.

### The plan reports a protected inventory block

Inspect the bot's bags. Quest items, consumables, the hearthstone, useful equipment, and other mandatory inventory are deliberately protected. The planner will not claim that reward space can be created by destroying those items.

### Build warnings about macro redefinition or DLL linkage

The current TrinityCore/TSWoW build can emit `C4005` and `C4273` warnings while still producing `WorldBots.dll`. Treat a non-zero build exit code, compiler error, or linker error as the failure condition.

## Relevant source files

- Standalone scenarios: [WorldBotsLogicTests.cpp](../tests/WorldBotsLogicTests.cpp)
- Pure planning contract: [TownPlanning.h](../livescripts/Town/TownPlanning.h)
- Pure planning implementation: [TownPlanning.cpp](../livescripts/Town/TownPlanning.cpp)
- In-realm scenarios: [ScenarioRunner.cpp](../livescripts/Testing/ScenarioRunner.cpp)
- Command routing: [HelperBindings.cpp](../livescripts/Bindings/HelperBindings.cpp)
- Runtime configuration: [BotConfig.h](../livescripts/Config/BotConfig.h)
