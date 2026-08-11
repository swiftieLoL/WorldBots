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
   - Each substate in `BotBlackboard` manages its own `refreshIntervalMs` and subtractive accumulator `elapsedMs`:
     - `SelfState`, `CombatState`, `NavigationState`: Refreshed @ 100ms.
     - `SpatialState`: Refreshed @ 200ms.
     - `QuestState`: Refreshed @ 1000ms (Stores discovered quest givers, turn-ins, and active quest logs without duplicating static `QuestTemplate` data).
     - `InventoryState`: Refreshed @ 1000ms.

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

11. **Inventory Capacity and Recovery Reserves:**
   - New bots and existing bots with empty bag slots receive configured starter
     bags without replacing populated containers.
   - Low-space cleanup retains one best usable food stack and, for mana users,
     one best usable drink stack. Other recovery stacks become vendor-eligible;
     active quest items and the Hearthstone remain protected.

12. **Bounded Progression Fallback:**
   - Quest selection applies a configurable solo level ceiling before distance
     scoring. Unsuitable quests remain in the log but do not monopolize work.
   - `GrindAction` resolves live creatures from blackboard GUIDs, validates
     hostility, exact level, normal rank, combat ownership, and loot ownership,
     then delegates combat to the existing class strategy.
   - Repeated deaths, blocked quest execution, or the logical-progress
     watchdog may require one gained level before the failed quest is retried.
     Static spawn data is used only to choose same-map hunting destinations;
     it never authorizes an attack.

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
5. **Reusable combat behavior:** class-specific strategies contain genuinely
   distinct rotations. Classes without one use `BasicMeleeStrategy`, and
   ranged classes share `CombatPositioning`.
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
│   ├── Blackboard/           # Statically-typed cached perception state (DATA ONLY)
│   │   ├── BotBlackboard.h   # Sub-states (Self, Spatial, Combat, Nav, Inv, Quest)
│   │   ├── BlackboardUpdater.h / .cpp (Coordinates timed sub-state refresh)
│   │   ├── QuestTargetResolver.h / .cpp
│   │   └── QuestItemSourceResolver.h / .cpp
│   ├── Brain/                # Perception & decision-making engine
│   │   ├── BotGoal.h         # High-level goals (Idle, Wander, MoveToNpc, Combat, Flee, AcceptQuest, TurnInQuest)
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
| **SenseTask** | 50 ms (20 Hz) | Service blackboard substate refresh timers |
| **ThinkTask** | 500 ms (2 Hz) | Evaluate goals and select actions |
| **ActionTask** | 50 ms (20 Hz) | Tick the active action and then advance movement |
| **DebugPositionTask** | 2000 ms (0.5 Hz) | Optional position diagnostics |
| **SaveTask** | Configurable; 500 ms default | Persist a bounded round-robin batch when progress saving is enabled |

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
                                 BlackboardUpdater
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
