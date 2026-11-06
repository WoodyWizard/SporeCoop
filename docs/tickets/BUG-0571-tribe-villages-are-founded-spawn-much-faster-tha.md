---
id: BUG-0571
title: Tribe: villages are founded (spawn) much faster than in the single-player game
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: host
versions: 0.7.5
fixed_in: 0.7.8
branch: fix/w44-tribe-guest
commits: c9abc726
tests: tribe_form_pace (tools/tests/coop_tribe_form_pace.py)
signature:
docs: docs/re/tribe.md#tribe-guest-a1
related: BUG-0483
---

## Report
The host: villages in Tribe spawn so much faster; on Discord: the villages spawn so fast they cannot get through the tribal stage.
Reports: SporeCoop-report-2026-11-04 191714 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (guest log): forms_blocked=61, npc_tribes_made=4 (the guest's own forming is blocked, BUG-0483; the
  4 tribes are the host's). The host's game formed them; its log has no forming line.
* Inferred: the host's three reloads (s 5850, 6391, 7320) and its gifts are not the cause shown; the NPC tribe
  controller's pace with two players' members in one tribe was not measured.
2026-11-04 (fix/w43-realign): not reproduced. Observed (lab tribe fixture): the host's world forms NPC tribes with its own controller (`coop.tribe form`: 2 -> 3, the planet then lists 5 villages; the save had 2); a guest forms none (tribe.guest_tribes). Inferred: every host game over reloads a save with fewer villages, and the controller forms them again ("fewer than its property list allows", docs/re/tribe.md#tribe-guest-world) at its own pace: three reloads in 25 min looked like fast spawning. Not measured: the controller's interval with two players' members in one tribe.

2026-11-05 (fix/w44-tribe-guest, A1 of 0.7.8):
* ✔ (Ghidra) the NPC tribe controller's update 0xCC6A90 (vt 0x10): every second it counts the tribe nouns; 1 tribe: the
  first neighbour after 25 s (properties 0x3D346A77 / 0x1A09D79E); **2 tribes: the third when the goal history is not
  empty (TribeStage +0x40, a village allied or conquered) or the player tribe's food (its vt 0xA4) is over property
  0x4C87E711 (default 100)**; 3 or 4: at once, every second (FormTribe 0xCC6440 then limits by its property list and the
  spawn markers); 5: when 3 goals are done.
* **Observed** (lab lane 9, tribe fixture, 2 tribes): no tribe in 5 min idle (food 15; out/lab/20261105-112506). With
  the food set to 135: villages 3, 4 and 5 within 2 s, **alone (both left the session) and in a session alike**
  (out/lab/20261105-113623 solo, out/lab/20261105-113252 co-op), and the guest's game had the host's within 2-4 s; the
  guest's controller forms none (tribe.guest_tribes). So the controller's rule and the host's ownership are unchanged
  by the co-op.
* **Observed** (player logs): the host's villages 3-5 came 3.8 min into Tribe, all three within 2 s (the guest's
  "made it" lines 19:01:36-38) while the party's food read about 50: there the trigger was a goal (TribeStage +0x40),
  or the food (vt 0xA4) is not exactly the shared food shown. ~ Inferred: in a session the food and the goals are the
  whole party's (every player's members gather into the one player tribe, the co-op's own rewards add to it: an epic
  killed together 80 food, a partner's heal 5), so the controller's threshold comes after a fraction of one player's
  time; the host's three game overs also reloaded worlds whose villages the controller formed again.

## Fix
`tribe.form_pace` (tribe.cpp h_Form, host only): with 2 tribes, no goal done and 2 or more players in the session, the
third village is formed on food only when the player tribe's food (the controller's own vt 0xA4) is over the threshold
(the controller's property 0x4C87E711 through SporeCoop::App_GetPropInt) times the players: the pace of one player's
tribe. A goal done still forms it at once (the game's rule). Decided once a second while held. `coop.fix
tribe.form_pace off`: the game's threshold for the whole party (0.7.7).

## Verification
tribe_form_pace: PASS out/lab/20261105-114858 (food 135 for 25 s: 2 villages on both; food 230: 5 on both 4 s
later); FAIL with SPORE_FIX_OFF=tribe.form_pace out/lab/20261105-115029 (5 villages on both within the 25 s at 135).
Not reproduced in the lab (a 10-minute forming count alone vs in a session was not run). Package W2 (plan table).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), not reproduced.
- 2026-11-04 (fix/w43-realign): analysed, not reproduced, not fixed (no change to the host's controller).
- 2026-11-05 (fix/w44-tribe-guest, A1 of 0.7.8): the controller's rule read and measured alone vs in a session (same); the party's food pace held to one player's by tribe.form_pace.
- 2026-11-05: released in 0.7.8.
