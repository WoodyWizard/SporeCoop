---
id: BUG-9195
title: Creature: a late joiner who finished the editor is sent to the galaxy menu and gets the world again
status: fixed
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 64c3afed
tests: tools/qa_win (to run: a joiner's first creature)
switch: ui.reload_cover
signature:
docs:
related: BUG-9128 BUG-9167 BUG-9193
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "When MSIJoiner finished creating their creature, they were spawning in on Host's nest, then got sent to the galaxy menu before being resent the world and rejoining and being at the Host's nest instead of their own still."

## Root cause
Observed: by design a new nest reaches a guest only with a fresh world (home_nests.cpp: a nest made on a guest crashed, BUG-9128): host 20:39:39 "seat 2's world goes out 4 s after its nest was made", joiner 20:39:52 the galaxy menu and the reload. The spot after it: BUG-9193 (1). MSIGuest loaded twice too: its join share was saved before its nest existed.

## Fix
ui.reload_cover (on, owner 2026-11-13: "we can do our loading screen to overlay it"): the guest's loading screen (ring, "Bringing your nest" / "Getting the latest world", the steps) covers a world arriving mid-stage, from its first byte until the guest plays the stage again (2 s settled, 120 s at most), so the galaxy menu and the load are not seen. The earlier notice was dropped. The spot is BUG-9193's fix. Open: making a joiner's nest before its first share (one load instead of two).

## Verification
no-fail-run: the reload stays by design; the cover is to be checked live (a joiner's first creature).

## History
- 2026-11-13: opened.
- 2026-11-13 (later): the notice replaced by the loading screen over the reload (ui.reload_cover).
- 2026-11-14 (3Q A): a plain rejoin loads once (Tribe, TITAN: leave, join: Tribe -> galaxy menu -> Loading -> Tribe, one
  world received). The second load is only a Creature late joiner's (its new nest needs a fresh world, BUG-9128). A
  joiner who brings a creature could load once if the host waited for its creature and nest (with a member, BUG-9167)
  before the first share; a joiner without one gets the editor inside the world (join_editor_tick needs the Creature
  stage), so it keeps two loads. Decision 3S.
