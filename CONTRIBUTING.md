# Contributing to WorldBots

Thank you for your interest in contributing to **WorldBots**! This project provides autonomous player bots for TrinityCore (3.3.5a WotLK) and the TSWoW framework.

To keep the codebase maintainable, reliable, and performant at scale (supporting hundreds to thousands of concurrent bots), all contributors must adhere to the following architecture rules and guidelines.

---

## 1. Core Architectural Rules

### Main-Thread Mutation Rule
> **Critical:** ALL TrinityCore entity manipulations (`Player`, `Creature`, `Unit`, `MotionMaster`, `Map`, `ObjectAccessor`, `WorldSession`) MUST occur exclusively on the **Main Server Thread**.

- Asynchronous background threads may perform heavy computations (such as async pathfinding or utility scoring), but the results must be posted back and applied during main-thread tasks.

### Memory Pointer Safety (`ObjectGuid`)
> **Never hold raw entity pointers across execution ticks.**

- Do **not** store raw pointers (`Player*`, `Creature*`, `Unit*`) inside actions, blackboard states, or scheduler tasks across ticks.
- Store `ObjectGuid` instead.
- On each tick, re-resolve and validate pointers on the main thread:
  ```cpp
  Player* bot = ObjectAccessor::FindPlayer(botGuid);
  if (!bot || !bot->IsInWorld() || bot->IsDuringTeleport()) {
      return; // Safely skip execution
  }
  ```

### Data-Only Bot Blackboard
- `BotBlackboard` is a pure data container.
- **No behavioral methods** are allowed in `BotBlackboard` (e.g. no `blackboard.FindNearestEnemy()`).
- Maintain strict separation of concerns:
  - **Sensors / Updaters (`Sense/`)**: Populate data.
  - **Brain (`Brain/`)**: Evaluates goals and selects actions.
  - **Actions (`Actions/`)**: Execute decisions via `MovementManager` and game APIs.

### Decoupled Task Scheduler
- Subsystems do not run arbitrarily on every frame tick (`World.OnUpdate`).
- All subsystems are registered inside the generic `Scheduler` running at tuned update intervals (e.g., 50ms, 100ms, 1000ms).
- Timing must use subtractive accumulation (`elapsed -= interval`) to preserve alignment across lag spikes.

### Header Dependency Rule
- In TSWoW / TrinityCore, inline methods in `Player.h` reference `sObjectMgr`.
- Any file including `#include "Player.h"` **MUST** include `#include "Globals/ObjectMgr.h"` prior to `Player.h`.

---

## 2. Coding Standards

- **Language Standard**: Modern C++20 (`/std:c++20`).
- **Header Guards**: Use `#pragma once` at the top of every header.
- **Naming Conventions**:
  - Types, Structs, Classes, and Methods: `PascalCase` (e.g., `BotBrain`, `EvaluateGoals`).
  - Local Variables & Parameters: `camelCase` (e.g., `botLevel`, `targetGuid`).
  - Private Member Variables: `_camelCase` (e.g., `_activeAction`, `_blackboard`).
  - Enums: `PascalCase` enum types with `PascalCase` values.
  - Constants: `PascalCase` or `kPascalCase` (e.g., `MaxGroupSize`).
- **Include Order**:
  1. Primary module header (e.g., `PartyRecruitmentPolicy.h` in `PartyRecruitmentPolicy.cpp`).
  2. Core TrinityCore headers (with `Globals/ObjectMgr.h` before `Player.h`).
  3. Module subsystem headers.
  4. Standard library headers (`<vector>`, `<string>`, `<algorithm>`, etc.).

---

## 3. Testing & Verification

Before submitting any Pull Request:

1. **Standalone Logic Tests**:
   Run the test suite to verify no regressions in goal evaluation, candidate scoring, travel heuristics, or town planning:
   ```powershell
   cmake --build <build-dir> --target WorldBotsLogicTests --config RelWithDebInfo
   ./RelWithDebInfo/WorldBotsLogicTests.exe
   ```
2. **Compilation**:
   Ensure the project builds with 0 errors across the configured compiler (`MSVC` / `Clang`).
3. **No Hardcoded Paths**:
   Ensure no local machine paths, usernames, or absolute filesystem paths are introduced.

---

## 4. Submitting Changes

1. Fork the repository and create a feature branch (`feature/your-feature-name`).
2. Commit your changes with concise, descriptive commit messages.
3. Push to your fork and submit a Pull Request against the main branch.
4. Provide a clear summary of the changes and testing results in your PR description.
