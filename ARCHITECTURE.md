# WorldBots Architectural Specification & Subsystem Design

This document serves as the authoritative architectural blueprint for the `WorldBots` framework. All future features, subagents, and chat sessions working on this repository should align with the rules and structures defined here.

---

## 1. Core Principles & Threading Rules

1. **Main-Thread Mutation Rule:**
   - **All TrinityCore entity manipulations** (`Player`, `Creature`, `Unit`, `MotionMaster`, `Map`, `ObjectAccessor`, `WorldSession`) MUST occur exclusively on the **Main Server Thread**.
   - Background worker threads may compute heavy tasks (e.g., GOAP planning, utility scoring, path generation) asynchronously, but MUST post results back to main-thread tasks for application.

2. **Decoupled Task Scheduler:**
   - Systems DO NOT run arbitrarily on every frame tick (`World.OnUpdate`).
   - All subsystems are registered as discrete tasks inside a generic `Scheduler` running on the main thread at tuned update frequencies.

3. **Drift-Free Overshoot Preservation:**
   - All scheduled tasks preserve timing alignment over lag spikes using subtractive accumulation (`elapsed -= interval`) rather than resetting to zero (`elapsed = 0`).

4. **Data-Only Bot Blackboard (No "God State"):**
   - The `BotBlackboard` is a **pure data container** holding cached state only.
   - **DO NOT** add behavioral logic or action methods to the Blackboard (e.g., `blackboard.FindNearestEnemy()`, `blackboard.MoveTo()`, `blackboard.Attack()`).
   - Maintain strict separation of concerns:
     - **Sensors / Updaters:** Populate the data (`BlackboardUpdater`).
     - **Brain:** Evaluates state and makes decisions (`BotBrain::EvaluateGoals()`).
     - **Actions / Managers:** Execute decisions (`BotAction`, `MovementManager`).

5. **Substate Refresh Timers:**
   - Each substate in `BotBlackboard` manages its own `refreshIntervalMs`,
     coalescing accumulator, initialization flag, and snapshot age:
     - `SelfState`, `CombatState`, `NavigationState`: Refreshed @ 100ms.
     - `SpatialState`: Refreshed @ 200ms.
     - `QuestState`: Refreshed @ 1000ms (Stores discovered quest givers, turn-ins, and active quest logs without duplicating static `QuestTemplate` data).
     - `InventoryState`: Refreshed @ 1000ms.
   - Deferred ticks perform one live refresh and preserve only the interval
     remainder. Think and Action do not consume an incomplete or stale initial
     snapshot; generation and substate ages are exposed in bot status.

6. **Centralized Bot Lifecycle:**
   - `CoreLogic` owns one runtime context per bot GUID.
   - Login, hot-reload, invalidation, shutdown, and re-registration must be
     idempotent. Actions must not create or destroy bot sessions themselves.

7. **Movement Command Ownership:**
   - Brain actions issue normal movement intents.
   - Explicit TypeScript movement commands acquire an external movement lease;
     brain actions cannot overwrite that lease until `BotStop` releases it.

8. **Isolated Factory Accounts:**
   - Dedicated mode assigns one marked authentication account to each bot slot.
   - The factory never adopts an account-name collision unless the account has
     the WorldBots ownership marker and contains only the expected character.
   - Migration from the configured legacy account is limited to deterministic
     managed bot names and preserves the character GUID and progression.

9. **Bounded Factory Preparation:**
   - Account creation, ownership migration, and character creation are prepared
     in a rate-limited queue serviced by `SpawnTask`.
   - Large test populations must not perform an unbounded synchronous database
     burst during module initialization.

10. **Bounded Login and Persistence Load:**
   - Only a configured number of asynchronous character login pipelines may be
     in flight. Transient failures return to the factory queue with capped
     exponential backoff.
   - Active characters are saved through a round-robin batch queue rather than
     in one population-wide database burst.

11. **Budgeted Runtime Fairness:**
   - Sense, Think, Action, and Maintenance tasks rotate through a stable bot
     roster instead of processing the complete population unconditionally.
   - Each pass stops at both a bot-count limit and a monotonic elapsed-time
     budget. Deferred bots receive the full accumulated task delta when their
     turn arrives, preserving timers while protecting the server tick.

12. **Inventory Capacity and Recovery Reserves:**
   - New bots and existing bots with empty bag slots receive configured starter
     bags without replacing populated containers.
   - Low-space cleanup retains one best usable food stack and, for mana users,
     one best usable drink stack. Other recovery stacks become vendor-eligible;
     active quest items and the Hearthstone remain protected.

13. **Bounded Progression Fallback:**
   - Quest selection applies a configurable solo level ceiling before distance
     scoring. Unsuitable quests remain in the log but do not monopolize work.
   - `GrindAction` resolves live creatures from blackboard GUIDs, validates
     hostility, exact level, normal rank, combat ownership, and loot ownership,
     then delegates combat to the existing class strategy.
   - If a non-quest grind target kills a bot, its persistent spawn is suppressed
     for five minutes and its creature entry for fifteen minutes for that bot.
     Both nearby selection and cached hunting-ground relocation skip them;
     active quest targets are exempt.
   - Repeated deaths, blocked quest execution, or the logical-progress
     watchdog may require one gained level before the failed quest is retried.
     Static spawn data is used only to choose same-map hunting destinations;
     it never authorizes an attack.
   - Three deaths inside five minutes trip a bot-local circuit breaker. The bot
     returns to its bind point, repairs and heals fully, and pauses proactive
     travel and combat for five minutes while retaining personal self-defence.
     Generic recovery never abandons a quest; only a death during active quest
     execution may temporarily suppress that quest.

---

## 2. Current Implementation Boundaries

The native runtime follows these additional boundaries:

1. **Single runtime aggregate:** `CoreLogic` owns one `BotRuntime` per bot. The
   aggregate contains the bot's brain and movement manager, so registration,
   pruning, shutdown, saving, and debug access cannot leave parallel maps out
   of sync.
2. **Typed decision boundary:** `BotBrain` converts the selected goal and its
   blackboard state into an `ActionRequest`. `ActionFactory` only constructs an
   action from that request; it does not rescan the world or reinterpret goals.
3. **Shared interaction contract:** quest, vendor, and other NPC actions use
   `NpcFinder` to classify an interaction as ready, requiring movement, or
   invalid. Inventory traversal is centralized in `InventoryUtils`.
4. **Focused quest resolution:** `QuestTargetResolver` handles exploration and
   quest-ender selection, while `QuestItemSourceResolver` handles item source
   discovery and caching. `BlackboardUpdater` remains the coordinator.
5. **Reusable combat behavior:** all nine playable classes have dedicated
   strategies with class-specific rotations, defensive priorities, resource
   use, and positioning. `BasicMeleeStrategy` is only the unknown-class safety
   fallback. Ranged classes share `CombatPositioning`; Hunter uses its range
   band support to escape the auto-shot dead zone. Hunter and Warlock also
   command an existing combat pet through TrinityCore's normal pet AI.
6. **Implementation out of headers:** `MovementManager` exposes a compact
   declaration header and keeps pathing implementation in its translation
   unit, reducing compile coupling.
7. **Private native build surface:** target definitions, include paths, and
   compile features are private to the module target. New native subsystem
   translation units are discovered at the module's CMake configure boundary.
8. **Slot-based roster profiles:** bot slots select from a repeating roster of
   race, class, gender, and behavior profile definitions. Behavior profiles
   currently tune flee and recovery thresholds without adding logic to the
   data-only blackboard.
9. **Cooperative parties:** `PartyState` caches deterministic tank, healer, and
   damage assignments, formation slots, the leader's active quest IDs, one
   shared combat target, and a designated resurrector. Followers prioritize
   shared quests, regroup at a bounded leash, assist the same target, and let
   class strategies retain responsibility for the actual combat rotation.
10. **Explicit policy and maintenance boundaries:** immediate combat/recovery
    arbitration lives in the pure `GoalPolicy`; terminal outcome normalization
    and suppression durations live in `FailurePolicy`. Spell learning, talents,
    equipment, riding, and mount upkeep run in `MaintenanceTask`, not Sense.
11. **Cached world discovery:** nearby quest sensing remains live, while global
    starter fallback uses per-map indexes plus positive and negative 30-second
    caches. Bots without local work therefore do not scan every quest relation
    and spawn on every quest refresh.
12. **Session ownership boundary:** `BotAuth` alone logs out and destroys owned
    socketless sessions. Adopted hot-reload sessions are detached without being
    logged out by WorldBots shutdown and are never ticked by WorldBots' private
    socketless-session loop.
13. **Typed action results:** terminal actions report an outcome, failure
    category, recovery directive, related quest/NPC context, and a diagnostic
    reason. `FailurePolicy` decides suppression or progression fallback from
    those typed facts; a generic `Blocked` result does not imply grinding.
14. **Capability-based town planning:** selling, repair, restock, and quest
    reward-space creation are planned independently. Vendor discovery verifies
    the specific capability required for each phase, and restocking follows
    reward collection so it cannot consume reserved turn-in capacity.
    A bot that dies after a service route is interrupted by combat suppresses
    that NPC entry for five minutes; planning and the nested vendor action both
    honor the per-bot exclusion and choose another service NPC when available.
15. **Bounded shared and negative caches:** party roster/role/leader-quest facts
    are shared briefly per group and live players are resolved by GUID for each
    bot. Runtime negative caches use monotonic expiry, prune old entries, and
    expose explicit per-bot or shutdown cleanup where ownership requires it.
16. **Quest-first progression and area migration:** the brain completes an
    actionable active quest first, then accepts suitable nearby work, then
    treats a reachable cached world starter as the migration destination when
    local questing is exhausted. A deferred or unsupported quest excludes only
    that quest; it does not prevent discovery of unrelated work. World-starter
    ranking prefers the current map, then the closest quest level, then travel
    distance. `GrindAction` runs only after active, nearby, and reachable remote
    quest work have all been exhausted. Before committing to a quest with a
    travel destination, `WorldTravel` verifies that the first bounded ground
    leg is executable. A first failure cools down the destination hub for 30
    seconds; a second failure within ten minutes suppresses the hub for 15
    minutes so other quests at the same unreachable giver cluster cannot
    churn. Nearby sensing and cached world-starter discovery consume the same
    per-bot hub suppressions. A failed Hearthstone transition is attempted only
    once per journey and is then removed from replanning so an alternative
    route can win.

These boundaries are intended to keep the Sense -> Think -> Action behavior
stable while making ownership, reuse, and future testing clearer.

---

## 3. Directory Structure & Subsystem Boundaries

```
modules/WorldBots/
├── livescripts/
│   ├── Actions/              # Action abstraction layer
│   │   ├── BotAction.h       # Base action interface (Start/Update/Stop/IsComplete)
│   │   ├── MoveToAction.h / .cpp
│   │   ├── FollowAction.h / .cpp
│   │   ├── WanderAction.h / .cpp
│   │   ├── FleeAction.h / .cpp
│   │   └── IdleAction.h
│   ├── Auth/                 # Session creation & character DB login queries
│   │   ├── BotAuth.h
│   │   ├── BotAuth.cpp
│   ├── Bindings/             # TS <-> Native C++ FFI binding bridge
│   │   ├── HelperBindings.h
│   │   ├── HelperBindings.cpp
│   │   └── HelperBindings.d.ts
│   ├── Blackboard/           # Pure data container (NO behavior, NO logic)
│   │   └── BotBlackboard.h   # Sub-states (Self, Spatial, Combat, Nav, Inv, Quest, Party)
│   ├── Sense/                # Perception orchestration layer
│   │   ├── SenseCoordinator.h / .cpp  (Timed sub-state refresh)
│   │   ├── QuestTargetResolver.h / .cpp
│   │   └── QuestItemSourceResolver.h / .cpp
│   ├── Brain/                # Decision-making engine
│   │   ├── BotGoal.h         # Goal enum definition
│   │   ├── GoalTier.h        # Goal priority tier contract (Survival -> Tactical -> Emergency -> Progression -> Coordination -> Fallback)
│   │   ├── QuestSelector.h / .cpp (Pure distance-scoring quest selection)
│   │   ├── ActionRequest.h     (Typed brain-to-action decision payload)
│   │   └── BotBrain.h / .cpp   (EvaluateGoals @ 500ms, request creation)
│   ├── Core/                 # Central bot framework controller & engine
│   │   ├── CoreLogic.h
│   │   └── CoreLogic.cpp
│   ├── Helper/               # Math, spatial lookups & pathing managers
│   │   ├── MovementManager.h / .cpp
│   │   ├── NpcFinder.h
│   │   └── NpcFinder.cpp
│   ├── Scheduler/            # Generic task scheduling engine
│   │   ├── ITask.h
│   │   ├── ScheduledTask.h
│   │   ├── Scheduler.h
│   │   └── Scheduler.cpp
│   ├── CMakeLists.txt
│   ├── livescripts.conf
│   ├── tsconfig.json
│   ├── bot_factory.ts        # Main TypeScript API export
│   └── livescripts.ts        # Module entry point
```

---

## 4. Subsystem Execution Frequency Matrix

| Subsystem Task | Frequency | Purpose / Responsibilities |
|---|---|---|
| **SpawnTask** | 100 ms (10 Hz) | Deferred character spawns and pending `BotAuth` session updates |
| **LifecycleTask** | 500 ms (2 Hz) | Validate active runtimes and recover sessions after lifecycle grace |
| **SenseTask** | 50 ms (20 Hz) | Service blackboard substate refresh timers |
| **ThinkTask** | 500 ms (2 Hz) | Evaluate goals and select actions |
| **ActionTask** | 50 ms (20 Hz) | Tick the active action and then advance movement |
| **MaintenanceTask** | 1000 ms (1 Hz) | Service level-driven spell and character maintenance separately from sensing |
| **DebugPositionTask** | 2000 ms (0.5 Hz) | Optional position diagnostics |
| **ProgressDiagnosticsTask** | Configurable; 60 s default | Optional durable latest/history snapshots for progression soak analysis |
| **SaveTask** | Configurable; 500 ms default | Persist a bounded round-robin batch when progress saving is enabled |

Sense, Think, Action, and Maintenance frequencies are target service intervals;
large populations are rotated under `RuntimeBotBatchSize` and
`RuntimeTaskBudgetMs`, so overload degrades update latency fairly instead of
creating an unbounded main-thread spike. Factory work has an independent
elapsed budget in addition to its operation count.

Combat, inventory, navigation, and stuck detection are deliberately executed
inside the Sense/Think/Action pipeline rather than as independent scheduler
tasks. Each scheduled task is bounded to a small number of catch-up
executions after a server hitch; excess backlog is discarded to protect the
main server thread.

---

## 5. Execution Flow & Substate Refresh Diagram

```
                                  SenseTask (50ms)
                                         │
                                         ▼
                                  SenseCoordinator
                                         │
           ┌─────────────────────────────┼─────────────────────────────┐
           ▼                             ▼                             ▼
       SelfState (100ms)             SpatialState (200ms)          QuestState (1s)
   (Health, Mana, Pos)          (Nearest Enemy/Friend)      (Available/Active/TurnIn)
           │                             │                             │
           └─────────────────────────────┼─────────────────────────────┘
                                         │
                         Consumed By ────┴────> ThinkTask (500ms) & ActionTask (50ms)
```
