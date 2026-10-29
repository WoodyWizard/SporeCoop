---
id: BUG-0071
title: shared diplomacy sends the allies mirror empire, so a guest can be allied with an NPC it never met
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#49
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-space
commits: 778e3e8
tests: space_diplo_mirror
signature:
docs: docs/re/space.md#space-ship-state-chunks
related: #49
switch: diplomacy.mirror
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/49 (full text, discussion).

`space_diplomacy.cpp` shares the host's whole relation table with the guests, and that table includes the mirror empire `space_allies.cpp` made for a partner's colonies. The mirror is "contacted" and allied by construction. A guest reconciles that record onto whatever empire has the same political id in its own galaxy. If that is a real NPC empire, the guest gets a first contact with it and an alliance it never made. Severity: wrong behaviour (relations of the player's own save changed; the guest saves them).

### Where
- the host table, no filter for mirror empires: [space_diplomacy.cpp#L220-L239](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_diplomacy.cpp#L220-L239) (contact flags [#L188-L205](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_diplomacy.cpp#L188-L205)), sent by [#L602-L641](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_diplomacy.cpp#L602-L641)
- the guest applies it by political id: [#L469-L533](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_diplomacy.cpp#L469-L533)
- events with the mirror are shared too: [#L319-L333](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_diplomacy.cpp#L319-L333)
- the mirror's id and its flags: [space_allies.cpp#L300-L316](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L300-L316), [#L261-L291](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L261-L291)

### How to reproduce
Two players, allies mode (rivals mode switches diplomacy off), Space:
1. The guest founds a colony on a free star (`coop.space allies colonize` or the colony tool). The host now has a mirror; note its pol from `coop.space allies list`.
2. On the guest, check `coop.space diplomacy list all` for an empire at that pol. If there is none, make the guest's game create one: visit stars with civilizations not yet met until a new empire appears at that pol.
3. Wait 20 s (or press Resync on the guest's Space page). On the guest, `coop.space diplomacy table` shows that empire allied and contacted, and the game's empire panel says so too.

### Expected
Mirror empires are local to each copy. Their relationship records and events are never shared; diplomacy shares only real NPC empires.

## Root cause
**What happens**

1. The guest founds a colony after joining. The host's copy of the galaxy does not have it, so `space_allies` on the host makes a mirror empire with `NextPoliticalID`, say pol P. `set_relation` sets cPlayer contact flags `|= 0x6` for P and the ally bit on the (P, host) relationship entry.
2. Every 20 s, and on a guest's Resync, the host's `send_snapshot` sends `my_table()`. `my_table` walks every relationship entry of the host's player, so (P, host) is sent with `flags = 2 | kContacted`. Nothing skips pols that `space_allies` owns.
3. On the guest, P is an independent number: each copy hands out ids from its own counter, and empires made after the join may get different ids (docs/known-issues.md "Space diplomacy", docs/re/space.md 15.1: NPC empires are created lazily with `NextPoliticalID`). P can be:
   - the guest's own mirror of the host's colonies (same flags in allies mode: no visible change);
   - nothing (the record counts as "unknown");
   - a real NPC empire the guest's game created later with that id.
4. In the last case `reconcile(h)`:
   - calls `o_Spoken(e)` (the first-contact talk) because `h.flags & kContacted` and the guest has not contacted it;
   - calls `o_Ally(mgr, e, me)` because the host's flags have the alliance bit;
   - sets the record's war/ally bits to the host's.
   The guest is now allied with an NPC it never met, and that goes into the guest's save.
5. Any relationship event between the host and its mirror (`h_ApplyEv`) is also sent as `K_EVENT` with npc = P, and the guest applies it to its empire at P.

### Confidence
CONFIRMED by reading: the mirror's record is sent and applied by pol. PLAUSIBLE: the collision with a real NPC empire on the guest. It depends on the guest's game creating an empire with the same id after the join; not run.

## Fix
Fixed on fix/w2-space (778e3e8). `spaceext::allies_mirror_pol(pol)` (space_allies) tells whether a pol is a mirror of this copy. `space_diplomacy` leaves such pols out of `my_table` (so of the snapshot and of the guest's "ours only" pass), does not send their events, state changes or contacts, and refuses to apply a partner's event / state / contact / table record onto its own mirror. `coop.space diplomacy mirrorfix 0` restores the old sharing. Keying records by the home star instead of the pol (the review's better option) is not done: a larger change of the protocol.

Suggested in the review:

- `space_allies` exports `bool is_mirror_pol(uint32_t pol)` (its `mirror_pol`).
- `space_diplomacy` skips such pols in `my_table` (and so in `send_snapshot`), in `h_ApplyEv` / `state_hook` / `h_Contact` / `h_Spoken`, and in `reconcile`, where the guest refuses to reconcile onto its own mirror.
- Better still, send records keyed by something both copies share (the empire's home StarID, `cEmpire +0xB0`) and resolve them per copy. That would also fix the "ids may differ" known issue.
- Keep the old behaviour behind `coop.fix diplo_skip_mirrors off`.

## Verification
Done (2026-10-28, lane 6): `coop_space_diplo_mirror.py` (suite `space_diplo_mirror`). The guest gets an empire of its own galaxy it never met (`allies fake` + `allies detach`), the host takes ids up to it (`allies nextpol bump`) and makes an allies mirror with the same id, the guest asks for the host's table. `--without-fix` FAILS: the guest's record of that empire becomes flags 34 (allied, contacted; "host table: 1 empires, 1 fixed"; scratch log `diplo--without-fix15117.log` (not kept)). With the fix PASS twice: flags 0, the host sends 0 empires.

Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_diplomacy.py`, new step "mirror not shared":
1. Two lanes. The guest colonizes a free star so the host gets a mirror at pol P.
2. On the guest, create or pick an NPC empire at P. The test can call `coop.space diplomacy local` on a freshly made empire, or spawn one with the game's `CreateEmpire` through a debug command.
3. Force a snapshot (`coop.space diplomacy sync` on the host).
4. Assert on the guest's game: the relationship flags of (P, guest) are unchanged and the cPlayer contact flags for P are unchanged (`coop.space diplomacy table` reads both from the game).

This must FAIL on main and with `coop.fix diplo_skip_mirrors off`, and PASS with the fix.

### Related
- docs/known-issues.md "Space diplomacy: what is not shared or may drift" (ids may differ per copy) and "Space allies" (the mirror's relationship entry stays in a save).
- docs/re/space.md 12 (diplomacy), 15.1 and 15.3 (mirror empire, contact flags).
- Same id-reuse family: the allies reload issue (old mirror empire reused at an old pol).

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `diplo--without-fix15117.log` (fix/w2-space, 0 -> 34 contacts), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261027-233002 on fix/w2-space (space_diplo_mirror ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #49.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-space (778e3e8): mirror pols are never shared or applied onto; test space_diplo_mirror FAIL without / PASS with.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: log paths of an agent's scratch folder replaced by the log names (BUG-0215: no paths outside the repository).
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
- 2026-10-28: switch: diplomacy.mirror (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
