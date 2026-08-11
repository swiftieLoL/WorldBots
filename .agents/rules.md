# Mandatory Workspace Rules & Lessons Learned Loading

> [!IMPORTANT]
> **Automatic Session Rule Loading**
> All AI assistants, subagents, and chat sessions working within this repository MUST read and adhere to the lessons learned, architectural guidelines, and threading rules defined in the following two authoritative documents upon session initialization:

1. **TSWOW Technical Best Practices & Gotchas**:
   - Location: [docs/tswow_lessons_learned.md](docs/tswow_lessons_learned.md)
   - Scope: Build scripts, execution environments (Datascripts vs Livescripts), DBC/SQL generation, C++ FFI bindings, database safety, Noggit RED MCP integration, and crash prevention.

2. **WorldBots Architectural Specification & AI Subsystem Lessons**:
   - Location: [docs/worldbots_lessons_learned.md](docs/worldbots_lessons_learned.md)
   - Scope: Main-Thread Mutation Rule, Decoupled Scheduler, Data-Only `BotBlackboard` pattern, symbol refactoring rules, `BotFactory` lifecycle, Questing vs Vendor logic priority, memory safety (`ObjectGuid` lookups), and movement lease ownership.

---

## Directives for AI Agents

- **Always Inspect Before Mutating**: Inspect `ARCHITECTURE.md`, `docs/tswow_lessons_learned.md`, and `docs/worldbots_lessons_learned.md` when designing features or debugging issues in this repository.
- **Never Violate Main-Thread Rules**: All entity state mutations (`Player`, `Creature`, `Unit`, `MotionMaster`) must be executed strictly on the main thread tick.
- **Never Hold Raw Pointer References**: Store `ObjectGuid` instead of raw entity pointers across frame ticks or async operations.
