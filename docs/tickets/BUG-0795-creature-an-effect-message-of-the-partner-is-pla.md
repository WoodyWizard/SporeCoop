---
id: BUG-0795
title: Creature: an effect message of the partner is played on a puppet the game freed between two frames (BUG-0091 sibling)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-freed
commits: 570ee491, fdc10ecc
tests: effect_puppet_gone
signature:
docs: docs/06-testing.md#testing-freed-block-reuse
related: BUG-0091, BUG-0426, BUG-0451, BUG-0520
---

## Report
No player report. A partner's creature effect (pool start / stop / one-shot) that arrives while the partner's puppet was just freed by a world teardown or by the game itself can crash the receiving game.

## Root cause
Observed at runtime (fix/w48-freed, 2026-11-12): with the guest's puppet destroyed by the host's game and the guest's pooled effect (pool 1, StartStopEffect) arriving in the same frame (`coop.puppet gamekillfx 1 E1A4BD93 0xC0FFEE01 4`), the host's game made an effect on the destroyed creature (the call returned an effect object; lab 20261112-012117). A one-shot effect (pool 3) is refused already by the bone check on a destroyed creature (`bone=` drops). No fault was seen in 8 s after the block was freed; the effect on a creature that is freed a few frames later is the use the ticket feared.

## Fix
mods/coop/effects.cpp: every effect message and the effect diagnostics take the partner's puppet through `puppet_live` (coop.cpp creature_puppet_live: a Creature puppet that is the live object made for it, puppet_now's world and serial check, and listed now); a puppet that is not is dropped and counted (`coop.effects`: gone=). modes.cpp creature_alive no longer trusts its once-a-frame note when game::listed is exact inside the frame (a destroy in the same frame is seen). Switch: `coop.fix effects.puppet_live off`.

## Fix idea
Take the puppet through coop.cpp puppet_now() and require creature_puppet(adapter) && game::creature_listed(puppet), as on_remote_anim does (count drops like g_anims_dropped); keep the old path behind a switch (effects.puppet_alive).

## How to test
Two instances in Creature. Guest keeps a looping effect running (sprint) and spams abilities; host `coop.puppet gamekill` (the game's own destroy of the guest's puppet) or a world share in the same frame. Assert on the host: no fault in the log / crash report, and a new counter of dropped effects > 0. Must fault or call into freed memory on main (with a probe that logs start_effect on an unlisted creature), pass with the fix.

## Verification
Test `effect_puppet_gone` (tools/tests/coop_effect_puppet_gone.py).
* FAIL (SPORE_FIX_OFF=effects.puppet_live): out/lab/20261112-012117 (the game made 1 effect on the destroyed puppet).
* PASS: out/lab/20261112-012019, out/lab/20261112-013003 (no effect made on it, no faults).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0520 (the game's UFO abduction freeing the partner's puppet on the host, duplicate of BUG-0507) is the known native free that reaches this path on 0.7.10+; BUG-0507's check sits in drive_puppet only. Linked both ways.
- 2026-11-12: fixed on fix/w48-freed (effects.puppet_live); the test idea of the closed-ticket review (a native destroy of the partner's puppet plus an effect in the same frame) is the test.
- 2026-11-12: released in 0.7.13.
