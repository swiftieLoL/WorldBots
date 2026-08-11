# Workspace Agent Rules & Mandatory Reading

Whenever a new AI chat or agent session starts in this project, the agent MUST read and apply the rules and architectural lessons documented in:

- [docs/tswow_lessons_learned.md](docs/tswow_lessons_learned.md) - Build pipeline, Datascript vs Livescript rules, C++ bindings, DB safety, Noggit MCP workflows.
- [docs/worldbots_lessons_learned.md](docs/worldbots_lessons_learned.md) - Main-thread entity rules, Decoupled Scheduler, `BotBlackboard` data pattern, memory pointer safety (`ObjectGuid`), Bot Factory lifecycle, Quest/Vendor state machine logic.
