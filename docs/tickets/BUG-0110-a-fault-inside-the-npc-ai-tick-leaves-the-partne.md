---
id: BUG-0110
title: a fault inside the NPC AI tick leaves the partner's puppet as the game's avatar for good
status: fixed
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#89
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-creature
commits: c068f08, 949c7f3
tests: npc_swap, unit (npc_tests: swap ledger)
signature:
docs: docs/re/creature.md#creature-pool-ids
related: #89
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/89 (full text, discussion).

Several detours swap game state around the call to the original function and put it back only when that call returns normally. The worst one is `h_NPCTickAI`: it writes the partner's puppet into `cGameNounManager::mpAvatar` (+0x54) for the length of one NPC tick. If the game faults inside that tick and its own main-thread handler swallows the fault, the stack unwinds past our clang frame and the restore code never runs. The game is then left with the puppet as its avatar, `g_swap_real` stays set and the mod's own view of the avatar goes stale.

### Where
- `h_NPCTickAI`: swap at L256-L257, restore at L268-L270 (only on normal return): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L243-L271
- `game::avatar()` returns `g_swap_real` whenever it is set: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L338-L351
- The same "swap, call, restore" pattern:
  - `h_NoAttack` writes the partner's cell index into G+0x411C (the cell avatar index): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L732-L743
  - `h_SelectUI` clears `+0xB58` bit 0x1 and sets it again afterwards: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L472-L489
  - `h_StageMsg` points `+0xE84` of puppets and proxies at a blank object: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L504-L535
  - `b_damage` sets `g_hit_by`, which attributes capture damage to a player: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L529-L541
- Nothing catches the fault on our side. SporeKit's vectored handler only logs and returns `EXCEPTION_CONTINUE_SEARCH`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/exceptions.cpp#L76-L160. The mod has no `__try` anywhere.
- The docs say the game's handler swallows main-thread faults: [docs/02-modkit.md L172](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/02-modkit.md#L172), docs/05-re-notes.md 5.5 ("Wine swallows the exception, the frame is aborted"), and the first-chance AVs in [docs/known-issues.md L85-L87](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L85-L87).

### How to reproduce
**How to reproduce / check**

- Code reading: no restore path runs if `o_NPCTickAI` does not return.
- Runtime (lab): in a creature-stage session, make the NPC AI fault while it targets a puppet. One way is a probe or a temporary devkit command that raises an AV inside the swapped call; another is an NPC whose AI reads a missing puppet field. After the fault, read `[cGameNounManager+0x54]` and compare it with the real avatar, and read `coop.status` (`ai_swaps` stops growing).
- Not verified yet:
  - which frame catches the fault (the game's own `__except` or Wine's callback wrapper);
  - whether playable frames follow;
  - whether a save written afterwards stores `mpAvatar`.

### Expected
After any return or unwind of the tick, `mpAvatar`, `g_swap_real`, the lent tuning and the cell avatar index hold their real values again.

## Root cause
**What happens**

1. `h_NPCTickAI` stores the real avatar in `g_swap_real`, writes the puppet `t` into `mpAvatar` and lends the avatar's `+0xE84` tuning to the puppet. Then it calls `o_NPCTickAI`.
2. Game code in that tick faults. This is the same code path whose crash class (0xD48634, 0xB88BEA, see docs/re/creature.md) is why the tuning loan exists. The game's handler catches the fault in an outer frame, and the unwind skips our frame.
3. Afterwards:
   - The game's `mpAvatar` is still the partner's puppet. Whatever the game does with its avatar (input, camera, AvatarTickAI, possibly a save) now uses our object. CLAUDE.md: "our objects never go into a save".
   - `g_swap_real` stays non-null. `h_NPCTickAI` only picks a target when `!g_swap_real`, so every later swap is off.
   - `game::avatar()` returns the stale `g_swap_real`. After a load or a stage change that creature is freed, so every caller of `game::avatar()` then uses freed memory.
   - The puppet keeps the avatar's tuning pointer.
4. The other detours leave smaller leftovers. `h_NoAttack` leaves the partner's cell as the avatar index of the cell game, which is as bad as above in the cell stage. The others leave a wrong flag bit, a blank `+0xE84` pointer, or a stale `g_hit_by`.

### Confidence
PLAUSIBLE, low to medium severity: a game fault inside the swapped call must happen first. The mechanism (restore only on normal return, no handler of ours) is confirmed by reading the code, and a second reviewer verified it independently. That reviewer also found the stale `game::avatar()` effect.

## Fix
Fixed for `h_NPCTickAI` (fix/w2-creature): the swap remembers the puppet and the lent tuning;
`game::swap_guard_tick`, called once per frame from `npc::tick_frame` (outside every NPC tick),
finds a swap still open, writes the real avatar back into `mpAvatar` (if the puppet is still
there), takes the lent +0xE84 off the puppet, clears the state and logs a warning
(`swaps_restored=` in `coop.npc`). `coop.npc swapguard off` keeps the old behaviour,
`coop.npc swapfault` makes the next swapped tick leave without its restore (tests).

The other detours (fix/w3-creature, 949c7f3): wave 2's guard did not cover them (it only knew the
NPC tick's swap). A ledger (`mods/coop/swap_ledger.h`, `game::swap_open` / `swap_close`): each detour
opens its change with an undo before the call into the game and closes it after the call;
`swap_guard_tick` undoes, once per frame, whatever is still open (`ledger_restored=` in
`coop.npc`, a warning in the log), and a detour that returns undoes what an inner call left open.
Covered: `h_NoAttack` (cell avatar index G+0x411C), `h_SelectUI` (+0xB58 bit 0x1), `h_StageMsg`
(the blank +0xE84, only where it is still the blank) and civ `b_damage` (`g_hit_by`).
`coop.npc swapguard off` also turns the ledger's per-frame undo off; `coop.npc swapfault ledger`
skips the next close (tests).

## Verification
Test `tools/tests/coop_npc_swap.py` (suite `npc_swap`), a test command that forces the condition
(the state an unwind leaves): the guest stands next to NPCs far from the host until `ai_swaps`
grows, then `coop.npc swapfault`; the game's avatar slot `[cGameNounManager::Get() + 0x54]` is read
between frames.

* FAIL without the fix (`--without-fix` = `coop.npc swapguard off`, lane 5, 2026-10-28, direct run):
  the slot stayed the puppet 0x1dd587d0 in 5 of 5 reads, ai_swaps stopped (399 -> 399).
* PASS with the fix: direct run (lane 5): the slot is the host's avatar, ai_swaps 1512 -> 3288;
  lab run out/lab/20261027-232407 (`npc_swap`).
* Not shown: a real fault inside NPCTickAI and which frame catches it.
* The other detours (fix/w3-creature): unit test `tests/npc_tests.cpp` `test_ledger` (make test): with
  the guard off a change left open by a skipped close stays (the old behaviour), with it the frame's
  tick puts it back; an outer close undoes an inner call's leftover. Not run in the game with a
  forced fault (`coop.npc swapfault ledger` exists for that).

### Related
#69 (freed puppet after a world teardown), #44 (puppet or proxy gets its AI back during a cinematic).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #89.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-creature for h_NPCTickAI (per-frame swap guard); test npc_swap. The other detours of the report stay open in their areas.
- 2026-10-28: fix/w3-creature: the other detours (h_NoAttack, h_SelectUI, h_StageMsg, b_damage) were not covered by the per-frame guard; a swap ledger covers them now. Unit test in make test (npc_tests: the guard off leaves the partner as the value, on puts it back; nested calls); no game test forcing a fault inside those detours.
- 2026-10-28: released in 0.7.3.0.
