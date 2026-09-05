# WorldBots Lessons Learned & Architecture Best Practices

This document summarizes key architectural principles, design lessons, memory safety rules, and AI subsystem patterns from the development and maintenance of the **WorldBots** module.

---

## 1. Core Architectural Principles & Threading Rules

> [!IMPORTANT]
> **Main-Thread Mutation Rule**
> ALL TrinityCore entity manipulations (`Player`, `Creature`, `Unit`, `MotionMaster`, `Map`, `ObjectAccessor`, `WorldSession`) MUST occur exclusively on the **Main Server Thread**.

```mermaid
flowchart TD
    subgraph Main Thread Tasks
        A[SenseTask @ 50ms] -->|Update Data| B(BotBlackboard)
        C[ThinkTask @ 500ms] -->|Read Blackboard| D(BotBrain Goal Selection)
        E[ActionTask @ 50ms] -->|Execute Actions| F[TrinityCore Entity Mutation]
    end
    subgraph Async Worker Threads
        G[Async GOAP / Path Computations] -.->|Post Results| Main Thread Tasks
    end
```

### Key Framework Rules:
1. **Decoupled Task Scheduler**:
   - Systems do not run on every frame tick (`World.OnUpdate`). Subsystems are scheduled inside a generic `Scheduler` running on discrete intervals.
   - **Drift-Free Timing**: Preserves interval alignment during lag spikes using subtractive accumulation (`elapsed -= interval`) rather than resetting (`elapsed = 0`).
2. **Data-Only Bot Blackboard (No "God State")**:
   - `BotBlackboard` is a pure data container. **No behavioral methods** are permitted inside Blackboard (e.g., no `blackboard.FindNearestEnemy()`).
   - Strict separation of concerns:
     - **Updaters / Sensors**: Populate data (`BlackboardUpdater`).
     - **Brain**: Evaluates goals (`BotBrain::EvaluateGoals()`).
     - **Actions**: Execute decisions (`BotAction`, `MovementManager`).
3. **Substate Refresh Frequencies**:
   - `SelfState`, `CombatState`, `NavigationState`: 100 ms.
   - `SpatialState`: 200 ms.
   - `QuestState`, `InventoryState`: 1000 ms.

---

## 2. Refactoring & Class Renaming Lessons

> [!NOTE]
> Refactoring lessons learned during class updates (e.g., `Bot` -> `Botdave` -> `Botuli` -> `Botharry`):

- **Header Guard & Namespace Integrity**:
  - Renaming core class symbols requires simultaneous updates across native C++ headers, `.cpp` implementations, Livescripts bindings (`HelperBindings.h/cpp`), and TypeScript `.d.ts` definitions.
- **Forward Declaration Hazards**:
  - Missing forward declarations during class renames cause opaque pointer cast warnings or silent incomplete type bugs.
- **Factory Registration Sync**:
  - Ensure static factory registries (e.g., `BotFactory` or `BotMgr` mappings) update string identifiers alongside class type names to avoid factory lookup failures at runtime.

---

## 3. Bot Factory & Lifecycle Management

- **Single Point Ownership**:
  - `CoreLogic` owns one runtime context per bot `ObjectGuid`.
  - Individual actions MUST NOT create or destroy bot sessions directly.
- **Idempotent Login & Despawn**:
  - Login sequence (`BotAuth`) handles database queries asynchronously.
  - If a bot despawns or teleports, pending tasks must invalidate gracefully without leaving dangling sessions in `CoreLogic`.

---

## 4. AI State Machine: Quest Logic vs. Vendor Logic

| Feature Area | Questing Logic | Vendor Logic |
|---|---|---|
| **Primary Goal** | Accept/Turn-in quests, hunt targets, explore areas | Buy food/drink/reagents, sell junk, repair durability |
| **Refresh Interval** | 1000 ms (`QuestState`) | 1000 ms (`InventoryState` / Durability check) |
| **Trigger Conditions** | Discovered quest givers, completed quest log entries | Bag space low (< 2 slots), durability < 20%, low food/water |
| **Movement Lease** | Acquired during NPC travel; released on interaction complete | Acquired during Vendor travel; released post-trade |
| **Priority in Brain** | Medium (Evaluated after Combat/Flee) | High-Medium (Evaluated above normal questing when broken/full) |

> [!WARNING]
> **Preventing NPC Interaction Lock-ups**
> Always verify target NPC interaction range (`<= 4.5 yards`) and check line-of-sight before opening dialogs or vendor windows. Reset active interaction state if the NPC moves away or enters combat.

### Inventory reserve policy

- A per-template rule such as "protect every consumable" cannot manage a full
  inventory because it has no knowledge of duplicate stacks or class needs.
- Build one short-lived inventory policy context per scan or vendor transaction.
  Retain the best usable food stack and retain a drink stack only for mana users;
  classify the remaining recovery stacks as surplus when bag space is low.
- Provision capacity by filling only empty equipped bag slots. Never replace a
  populated bag automatically, because doing so risks orphaning or losing its
  contents.

### Progression fallback policy

- A movement destination near a level-appropriate creature spawn is not a
  grinding implementation. The action must resolve and validate a live target,
  initiate combat, survive normal interruptions, and expose its state in
  diagnostics.
- Score quest suitability before distance. Otherwise the nearest high-level or
  repeatedly failing quest continuously wins selection even when safer work is
  available elsewhere.
- After a repeated-death or no-progress failure, a time-only suppression can
  recreate the same loop without changing the bot's power. Retrying after one
  gained level gives the recovery phase a measurable exit condition.
- Quest-ID suppression alone does not stop churn when several quests share one
  unreachable giver hub. Remember the destination geography per bot: briefly
  cool the hub after the first failed first leg, escalate after repeated
  evidence, and feed that same geography back into both local sensing and
  world-starter discovery.
- Synthetic travel edges need journey-scoped failure state. Blocking only
  static graph nodes cannot suppress a failed Hearthstone edge whose endpoints
  are rebuilt for every route; without an explicit journey flag, a 20-second
  timeout can silently repeat until the global replan budget is exhausted.
- Static creature templates may guide travel, but live attack authorization
  must still check spawned level, rank, hostility, current victim, and loot
  ownership on the main thread.

---

## 5. Memory Safety & Pointer Management

> [!CAUTION]
> **Never Hold Raw Entity Pointers Across Ticks**
> Storing raw `Player*`, `Creature*`, or `Unit*` pointers in actions or scheduler tasks across execution cycles leads to catastrophic segmentation faults when entities despawn or change maps.

### Technical Safety Guidelines:
1. **Store ObjectGuid Instead of Pointers**:
   - Store `ObjectGuid` inside actions, blackboard states, and target references.
   - Resolve pointers on the main thread tick via `ObjectAccessor::FindPlayer(guid)` or `map->GetCreature(guid)`.
2. **Nullity & Map Validation**:
   - Always validate pointer AND map status before dereferencing:
     ```cpp
     Player* player = ObjectAccessor::FindPlayer(botGuid);
     if (!player || !player->IsInWorld() || player->IsDuringTeleport()) {
         return; // Skip execution safely
     }
     ```

---

## 6. Movement & Pathfinding Guidelines

- **Movement Lease Ownership**:
  - `BotBrain` actions issue movement intents.
  - Explicit TypeScript or script commands acquire an external **movement lease**. Brain actions cannot override movement until `BotStop` explicitly releases the lease.
- **Stuck Detection Heuristics**:
  - Track bot positions in `SpatialState` @ 200ms. If position change is `< 0.5 yards` over 3 consecutive move ticks while velocity `> 0`, trigger stuck behavior (jump, random path offset, or MMAP path recalculation).
