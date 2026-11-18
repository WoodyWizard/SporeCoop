---
id: BUG-9056
title: Separate founder civilizations do not share native campaign progression
status: open
kind: bug
stage: Civilization
reported: 2026-11-05
source: Native two-founder Civilization integration
role: host guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests:
switch: civ.team_progress
signature: native city counts remain one per founder and capture paths compare one player nation
docs: docs/re/civ.md
related: BUG-9046 BUG-9054
---

The two capitals and normal vehicle controls work, but native campaign rules
still recognize a single player nation. Each founder's native city vector has
one capital. Shared money is not evidence of shared capture history, unlocks,
progress bar or Space readiness. Implement team credit without moving the
captured city into the other founder's nation or duplicating native city entries.

Initial code evidence: native GetCityCount BF0320 returns the country's vector
length. Capture BE9430 and ChangeOwner BE8A20 include player-only branches.
Qualify a guest-earned capture and subsequent host-earned capture, native
unlocks/history on both games, repeat/reconnect behavior, and the final stage
condition. Avoid changing city counts used as bounds for real city vectors.

2026-11-05 component qualification: m2-civ-team-progress-control.log FAIL
(guest capture leaves the home campaign counter zero). After the patch,
m2-civ-team-progress-fixed-d.log PASS: guest then host capture, automatic
checkpoints, native captures2 on both games, tech3/highest4, matching native
unlocks, two actual cities per nation, original species, zero faults after
the checks. Native spawn/capture calls are explicit fixture shortcuts, not
ordinary player-input combat evidence. Keep open for history/readiness.

Native history readback identifies the next gap: both capture events exist,
but AE3740 records home-only city counts1/2 instead of team counts3/4.

CORRECTION (2026-11-05, duo-species): earlier AE3740/67DE50 gameplay-history
claims in this section are superseded. The manager is telemetry, established
by telemetry.dat, App/Telemetry and native writer7EB6D0. Its stage3 records do
not prove player-visible timeline contents or a campaign-save defect. BUG-9058
is not reproduced. The experimental AE3740 hook was removed; no telemetry
rewrite or pre-save flush is shipped. Native capture totals, unlocks and city
ownership evidence remains valid. Player-visible history needs its own test.

six-fed-civ.log and six-fed-hud.log: the religious handoff produced one
capital for each original species. With team progress off, both native HUD
calls displayed 0.1. With team progress on, both displayed 0.2 against the
native target of ten cities, while each home-only getter still returned 0.1.
Both games exited with zero faults. Final Space readiness remains unqualified.

civ-ten-captures.log (Vantiegan) qualifies native readiness CF7730 on both
games. The guest and host alternated eight fixture captures. Readiness
remained false at three through nine combined cities. At ten it became true
on both games. Each founder retained five native cities; both had tech4,
highest10 and matching unlocks. The home campaign counter recorded eight
military captures and the partner's individual counter recorded four.
civ-ten-save.log confirms the native save before both games exited cleanly.
The capture fixture bypasses combat. Stage entry and diplomacy are separate checks.
