---
id: BUG-0218
title: a partner's puppet that fails the new identity check is left in the game as an armed NPC ship
status: fixed
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #152 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-space
commits: 730acef
tests: space_defenders (coop_space_defenders.py part 2 a, b, c)
signature:
docs: docs/re/space.md#space-object-serial
related: BUG-0111, BUG-0054, BUG-0230
---

## Report
From GitHub issue #152: https://github.com/WoodyWizard/SporeCoop/issues/152 (the full analysis, links and the scenario are there).

The BUG-0111 fix (eb7541e, 3984987, release 0.7.3.0) decides "is this still our puppet?" by the address, the model key, the invincible byte and the owner mission. When a real puppet fails only the flag part of that check, the mod forgets it but does not destroy it: the ship stays in the world with our political id, its own AI and its weapons, and the host sends it to the guests as one of its NPC ships. Severity LOW: nothing in the mod changes these flags on a real puppet (only the test command `coop.fleet impostor` does), so it needs the game to change one of them.

## Root cause
Observed: with the 0.7.3.0 check (flags in the identity) a cleared invincible byte on the guest's real puppet made the host forget it and spawn a new one; the old ship stayed and was mirrored to the guest as an NPC (guest type 6 mirrors 1 -> 2, lab 20261028-225557 with `fleet.puppet_id` off).

## Fix
Step 0 (static): cGameData mID +0x24 is `++[0x167BE28]` in CreateInstance (GOG 2024 0xB20C83), the only writer; the counter's address is also in two static tables (0x131AD5A, 0x131B076), maybe a serializer, so serials are not trusted across a load (not measured). `game::object_serial`. The puppet identity is `{model key, serial}`; a cleared invincible byte is set back (`flag_restored`); a puppet whose owner mission +0x800 is set is retired (`modes::space_retired`: held still, not sent by `host_tick`, its FireTool shots dropped, not swept on a guest, destroyed through `game::destroy_creature` once +0x800 is null, left to the game with a log line at session end); a different serial is forgotten as before (BUG-0111). Switch `fleet.puppet_id`. Test commands `coop.fleet impostor_id <p>`, `impostor_mission <p> [0 <ptr>]` (the mission is simulated in the mod, +0x800 is never written), `coop.fleet ident`.

## Verification
PASS out/lab/20261028-224835 ((a) same pointer, invincible 1, no extra mirror; (c) new puppet, old ship kept and not mirrored, destroyed when released; (b) new puppet, old ship left). FAIL with `SPORE_FIX_OFF=fleet.puppet_id` out/lab/20261028-225557 ((a) new puppet and an extra mirror on the guest; (c) no retire). Still unknown: whether the game itself ever changes +0x588 / +0x800 on such a ship.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-space (730acef): identity by object serial, retire for missions; defenders test reworked.
