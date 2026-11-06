---
id: BUG-9059
title: Partner village blocks the first native Tribal rival
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Fresh automatic-founder campaign completion regression
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_campaign.py
switch: tribe.founder_spawn_gate
signature: zero victories and only two founders, native formation calls zero
docs: docs/re/tribe.md
related: BUG-9047 BUG-9040
---

FAIL build/local/tribal-final-campaign-a.log: automatically founded Vantiegan
completed its native outfitter objective but no rival appeared within the
bounded test. Native founders and saved outfit models agree on both games;
both have zero victories and no native faults. Native FormVillage calls zero.

Decoded CC6A90 controller update calls noun GetData atCC6B5D, returnCC6B62,
consumes only vector length and branches on count minus one. Two total tribes
selects a later appearance condition despite the second tribe being a partner.
The existing BUG-9047 view only corrected the factory's count atCC647D, which
is downstream and cannot correct this earlier eligibility decision.

Extend the caller-scoped private count view to the verified eligibility
lookup. Native timers, prerequisites, placement and species selection remain
in control. Never manufacture a victory or remove the founder from noun data.

First patched run creates native rivals without a formation/spawn command.
Run B then fails in the test because a new guest mirror's local faction differs
before checkpoint; the driver now targets its replicated world position.
Run C earns the guest's first native victory, shared segment1 and unlocks
(1,5,6,7). Its host raid uses a guest-only command and is invalid evidence of
a host gameplay failure. Native enemies then capture the uncommanded host.
The guest's ordinary walk and rebuild restore three outfitted host citizens
on both games (tribal-final-rebuild.log), preserving conqueror ownership of
the old site and reporting zero faults. Campaign completion remains pending.

Qualification update (2026-11-05): tribal-final-campaign-g.log earned the
first native victory in the fresh automatically founded Vantiegan campaign.
tribal-final-campaign-h.log continued the same campaign through victories2-5.
All rivals appeared through the native controller; no FormVillage or progress
command was used. Food, attacker placement and nonlethal hut damage shortened
combat; native guest-ordered AI delivered every final blow. The guest also
helped refound the defeated host in tribal-final-rebuild-c.log.
tribal-final-five-readback.log independently verifies five native totem
segments, matching type/color arrays and unlocks on both peers, immutable
291489FF/7CF26CCB founder identities, living populations, and actual ownership
of both surviving huts. Zero recorded native faults. This closes the rival
appearance defect; it is not full campaign or two-PC qualification.
