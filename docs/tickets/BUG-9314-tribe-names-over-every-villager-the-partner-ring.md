---
id: BUG-9314
title: Tribe: names over every villager; the partner ring under the unit is enough
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-21
source: owner 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-tribe
commits: 3e6d1b0a
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
signature:
docs:
related:
---

## Report
Owner 2026-11-21: "in tribe stage, I saw text above my units, we shouldn't have that right? Our ring below the unit should be enough."
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
The owner saw name text over the villagers in Tribe (their own and the partner's: draw_owner_tags in coop.cpp draws the owner's name over every member, from before the rings).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
With the partner rings on (tribe.partner_ring, 7K A), draw_owner_tags draws no text; a downed villager keeps its revive ring timer (and the [REVIVE] text only when the ring timer is off). Switch `coop.fix tribe.ring_only off`: the names again. Branch feat/lineages-47.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (fix/l47-tribe 3e6d1b0a, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a
  live Creature -> Tribe entry from Vandh; the guest's villagers walked into the host's village, then the host's into the
  guest's view): PASS on both screens. Switch on (default): no text over any villager, the partner rings shown
  (host `coop.status` `labels: ... name_tags=0 partner_rings=3`, guest `name_tags=0 partner_rings=3`;
  out/qa-shots/bug9314_host_on.png, bug9314_9315_host_on.png, bug9314_9315_guest_on.png). `coop.fix tribe.ring_only off`
  on both: the names came back ("Host-NoShare-", "Host-NoShare-'s chief" on the host, "Host", "Host's chief" over the
  host's villagers on the guest; host `name_tags=3`, guest `name_tags=3`; out/qa-shots/bug9314_9315_host_off.png,
  bug9314_9315_guest_off.png); on again: gone (name_tags=0). A downed villager's revive ring was not checked.
no-fail-run: checked by hand in live two-game QA (no lab on Windows); the PASS evidence is above, the old behaviour stays behind the switch.

## History
- 2026-11-21: opened.
- 2026-11-21: live two-game QA on the MSI: PASS on, FAIL off (see Verification).
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
