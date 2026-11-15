---
id: BUG-9208
title: Creature: a guest arriving in a world new to it played its old creature and evolved at the host's nest (cocoon there) before its own nest existed
status: open
kind: bug
stage: Creature
reported: 2026-11-14
source: owner play test 2026-11-14 (night)
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9203 BUG-9207 BUG-9194 BUG-9182
---

## Report
Owner play test 2026-11-14 (night): when the guests connected to the new world, they "were playing their original creatures"; "It should bring up the starting base creature (random spine body). They shouldn't be sharing their creatures or have family at their nest until they finish creating their first creature." And: "When Host spawned in, the connecting players had their cocoons on the Host's nest, not their own."

## Root cause
Observed (logs, TITAN guest 8 and the host): the guests arrived 21:44:31-33 and played their co-op window picks ('Lilku', 'Duckulon' from choices.ini, BUG-0345); the family followed the old species (`evolve: the own family follows ... 4 member(s)`); the BUG-9203 editor opened from that pick 12-56 s later (MSI 21:44:50, Titan 21:45:28) while their bodies still stood at the host's nest (the spawn), so the partners' cocoons (BUG-9194) showed there. The seat nests came only at 21:47:16-18 (home_nests, after the host's own intro), made with the OLD species while the guests were in the editor. With BUG-9207 a guest connected before the host's new world arrives with its fresh creature and none of this happens; a guest joining a running world new to it still takes this path.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
- 2026-11-14 (night): opened from the owner's play test; the usual path (guests waiting while the host makes the world) is covered by BUG-9207; the late-join path (base creature editor at once, no old body, no cocoon at the host's nest) is next.
