# Generalisation plan (2026-11-01)

Five read-only audits (all tickets BUG-0001..0414, and the 2026-11-01 fixes in depth) found that most
gameplay fixes close the one reported case, while the same class of bug stays open elsewhere. This plan
groups the findings into classes. Each class gets ONE general fix at a point every user goes through, and
ONE outcome check (monitor rule, sync-check digest or test) that catches the class whatever the cause.
A class is done when its umbrella ticket's sibling list is empty or each item is marked covered with
evidence. Audit notes (not in git): out/ticket-audit-*.md and the session reports.

**0.7.4 order of work:** [sync-architecture-plan.md](sync-architecture-plan.md) (2026-11-02) explains why
these classes keep appearing (six structural causes) and orders the work as four layers (divergence
detector, lifecycle service, replicated state, ownership requests, chokepoint relay). The classes below
are the checklist each layer must close.

| Class | Umbrella ticket | General fix | Outcome check |
|---|---|---|---|
| G1 player-species / avatar-only rules | BUG-0418 | guest-side rule: the guest's own avatar species counts as the player species for every rule that reads PlayerSpecies 0x4DF490 / IsPlayerSpecies vt 0xD0 (audit each caller for host/save effects); run the game's own reward functions with the puppet as the avatar instead of re-implementing them | trace `own_rule` vs `as_player_species` (max HP, scale, damage) equal; puppet scale = player's own scale |
| G2 state sent but not applied | BUG-0419 | one table of every state field per object kind with its apply site per stage; apply health/scale/age everywhere | sync-check digest carries state fields (hp, max, dead, scale, age) and compares them; unit test fails on an unlisted field |
| G3 relation / ownership / world filter | BUG-0420 | `relation_allows(attacker, victim)` for every hit path in every stage; `is_playing_partner(id, in_my_world)`; world-state messages default-deny at dispatch (tag where defined) | monitor: no party/ally AI targets a partner, no posse damage in any stage; unit test: every MSG_ type classified |
| G4 guest-local objects removed or missing | BUG-0421 | keep guest-local objects by origin and role (family, mate, eggs, pack, part tokens, envoys, ambush) instead of id exceptions; per-kind presence digests | sync-check kinds for eggs, tribes, cities, buildings, tools, parts hash, relation table hash |
| G5 per-player lifecycle and resync | BUG-0422 | one per-player registry with join/leave observers (no id reuse within a session or a mandatory leave hook); receivers ask for a resync of unknown ids | 3-game test: B leaves, C joins as B's id, nothing of B shows; dropped-spawn test recovers within N s |
| G6 effects dropped or undone | BUG-0423 | ledgers pause, never drop (`have=false`); always ack; one owed-effect queue keyed by world identity ("pay when back in stage") | every credit path with the receiver in menu / editor / load: game values equal on both machines |
| G7 content one side lacks | BUG-0424 | `ensure_content(key)` with `key_exists` for every referenced id of every model kind (creature, cell, vehicle, building, UFO, city), substitute or refuse; transfer bld/vcl designs with id remapping | host creation with unknown part ids in Creature / Civ / Space: guest shows a model, no fault |
| G8 remote body placement and timing | BUG-0425 | one driver policy for every remote body (ground, trail/stall, root motion, speed cap, extrapolation limit); one CycleTracker for chunked snapshots; NaN/Inf guard in game::teleport/set_position/place; log oversize unreliable messages | monitor drawn / ground / proxy rules for every kind at 30 fps + jitter; chunk unit test (lost, late, older cycle, dup, wrap) |
| G9 object lifetime and identity | BUG-0426 | identity = pointer + serial everywhere; liveness by the game's noun list, not object_alive (lint); one registry of mod-made objects used by savestrip (all stages) and destroy; SaveGame hook independent of space_allies | chaos lane frees/recreates objects per stage, no fault; save/load per stage has no mod objects |
| G10 world identity and transitions | BUG-0427 | one verified world-identity service driving every on_world_change (space_ext, squadron, cellsync, social, parts, tribe, civ); no NPC rebuild on a null-local editor frame; every queued task tagged with session/world epoch | each real transition (editors, outfitter, adventure, load) keeps the epoch and the proxies |
| G11 busy states | BUG-0404 (in work) | one `game_busy()` (cinematic lock, paused, editor, loading) used by votes, editor requests, shares, census, sync-check | start each action during a cinematic / paused window: it waits and completes |
| G12 hand-copied game rules | BUG-0428 | call the game's own function (DamageCell, KillIfDead, CreatureSocialSuccess) with the puppet as actor instead of copies; forward hp at the write point | every damage type (bite, chunk, break, electric, poison, spikes) from the guest: host hp and death |
| G13 input from outside the game window | BUG-0429 | all mod input from the game window's messages while focused | key/click in another window changes nothing |
| G14 tests and harness | BUG-0430 | warm reset restores every switch and knob; lint asserts on coop.* counters; every test file in a suite; every fixed ticket names a switch or a reason; tests that cannot fail get a FAIL run | `make test` checks |
| G15 incomplete requests / objects the mod builds | BUG-0431 | build requests like the game (EditorRequest +0x90, flags, collectables vector, message 0x5D02A72); complete puppets/proxies (tuning block) | byte diff of the mod's and the game's request; every tuning reader runs on a puppet/proxy without fault |
| G16 capacity | BUG-0432 | inventory of fixed game arrays per stage; hard guards at the consumer; mirror budget near the camera | 4-player stress per stage, fault counters |
