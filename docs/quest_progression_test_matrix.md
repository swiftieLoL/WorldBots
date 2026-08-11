# Quest Progression Integration Matrix

Use this matrix against a small bot set after deploying a new WorldBots DLL. A scenario passes only when the quest counter changes or the quest is explicitly suspended with a reason; indefinite movement, combat, interaction, or action recreation is a failure.

| Scenario | Expected behaviour |
|---|---|
| Creature kill | Bot retains one target GUID, uses its class rotation, loots, and resumes the same quest. |
| Creature item drop | Failed drops do not reset the persistent 180-second progress watchdog; successful loot resets it. |
| GameObject objective | Bot travels to the correct GO entry and calls its native use interaction. |
| GameObject item source | ProgressQuest moves into range and yields to LootAction for inventory transfer. |
| Speak/cast-credit creature | Bot approaches and invokes TrinityCore talk/cast-credit handling without attacking it. |
| Exploration trigger/POI | Bot travels to the resolved area and completes the exploration event. |
| Empty scripted objective | After 30 seconds without credit, the quest is marked unsupported for one hour. |
| Missing spawn/source | Quest is blocked and suppressed instead of recreating ProgressQuest continuously. |
| Full bags before item objective | Bot preflights one objective item, yields to VendorAction before killing its source when result 50 reports full inventory, preserves quest items, then resumes the same quest context. |
| Caster combat | Mage/priest/warlock/druid/shaman/hunter strategy casts or closes range instead of standing at 25 yards with basic attack only. |
| Unexpected attacker | Actual attacker is handled before the objective target; quest context resumes afterwards. |
| Repeated path failure | Recovery movement survives for one second, then the exact selected quest is suppressed if failures continue. |
| Map 0 | Spawn lookup remains restricted to Eastern Kingdoms and never treats map 0 as “any map.” |
| Cross-map objective | Quest does not deadlock; it remains inactive until a travel route exists while other quests can be accepted. |
| Multiple enders | Nearest reachable creature or GameObject ender on the current map is selected. |
| GO starter/ender | Bot discovers, accepts, and rewards quests through quest-giver GameObjects. |
| Suppressed starter while wandering | After an accept/progress failure suppresses a quest, productive wandering ignores every starter for that quest until the suppression expires. |
| Unresolved first objective | When objective one has no same-map spawn but a later incomplete objective does, the bot selects and progresses the reachable objective. |
| Hostile creature objective | A hostile creature credit target is attacked and killed; it is never completed by calling the talk-credit API. |
| Friendly talk objective | A non-attackable creature credit target is approached and credited through `TalkedToCreature` without combat. |
| Cast-credit creature | A database-authored CAST objective uses the separate creature-credit path and is never reported as a conversation. |
| Shared item across quests | The same item used as a source item in one quest and loot objective in another keeps independent source-cache entries. |
| Live turn-in overrides static location | A nearby registered quest ender replaces an unresolved/static turn-in record with its live GUID and coordinates. |
| Unrelated reward NPC | A reward-ready NPC or GameObject is never attached to a completed quest unless its entry is registered as an ender for that quest. |
| Cave objective below bot | A vertically nearby objective with a long walkable route uses navmesh corner/partial paths to enter the cave; it never substitutes a direct shortcut through terrain. |
| Ranged target without LOS | A caster continues navigating toward a nearby objective hidden by cave terrain instead of stopping at preferred range and casting into the floor. |
| Long-distance accept/turn-in | Travel continues beyond 60 seconds while the bot is physically moving; it times out after 60 seconds without movement or at the 10-minute hard ceiling. |
| Segmented Wander route | Productive wandering retains its quest-starter intent across smooth-path segments and does not alternate with a settlement every few seconds. |
| Missing live vendor | Reaching a cached vendor coordinate without a live vendor performs no sale, repair, item destruction, or money change. |
| Dynamic vendor cache exclusion | Waypoint, random-wandering, and event-controlled vendors are never selected by their database spawn coordinates; they are usable only when found live nearby. |
| Inventory-blocked loot retry | After loot storage fails with full bags, only that corpse is deferred; a newly killed corpse remains eligible for one independent loot attempt. All blocked corpse records clear when actual bag capacity becomes available. |
| Long objective travel | Travel to a known objective area does not consume either quest no-progress timeout. The timeout starts after arrival in the objective area. |
| Interrupted quest watchdog | Loot, rest, combat diversion, and town service time do not count as active objective work. Returning to the quest preserves the prior objective-work budget. |
| Protected inventory cleanup | When policy prevents creating more reward space, inventory cleanup backs off without blacklisting the vendor NPC or destroying protected items. |
| Vendor inventory diagnostics | Remaining items log their real bag, slot, name, entry, and count; no `(undefined)` placeholder values appear. |
| Vendor cleanup target | A blocked corpse requests one free slot and poor zero-value discards stop after one slot; quest-reward cleanup uses its larger requested reserve. |
| Vendor/quest status drill-down | `.bot status vendor` explains every sell/protect decision and `.bot status quest` reports objective preflights, target resolution, watchdog state, and suppressions without changing the bot. |
| Death and graveyard repop | A socketless bot completes its graveyard teleport directly on the server, remains logged in with the same brain, resurrects at half health, and resumes normal goal selection. The lifecycle log must not repeat pending-teleport warnings every tick. |
| Transient lifecycle transition | A temporarily absent or teleporting Player receives 30 seconds of lifecycle grace. `.bot status` reports the preserved-brain transition; a session rebuild occurs only if the bot never returns. |

Useful status command fields are `Goal`, `Action`, `Quest Context`, and `Last Action Detail`. A blocked or unsupported quest should also emit one throttled suspension log containing its quest ID, duration, and reason.
