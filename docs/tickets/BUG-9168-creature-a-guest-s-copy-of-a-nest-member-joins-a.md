---
id: BUG-9168
title: Creature: a guest's copy of a nest member joins any nest herd of its species (an old or dead one), so a new nest stays empty there
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: qa
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + 2 TITAN guests + the MSI as a guest over Tailscale (relayed), 2026-11-13, all players with a creature of their own)
signature:
docs: docs/re/creature.md#creature-migration-own-nests
related: BUG-9008 BUG-9128 BUG-9135 BUG-9167
switch: npc.proxy_nearest_nest
---

## Report
QA: with BUG-9167 the MSI's new nest was sent with a member, yet on the MSI it still had "no members here yet" for minutes, while another guest's new nest got its member and was joined.

## Root cause
npc.cpp proxy_herd: a guest's copy (proxy) of a host nest member goes into "the nest herd of its species": the cached one or the first in the object list. With a nest per player and nests rebuilt (migration, a nest that died out) a species has several nest herds in the world, so the member went into an older one.

## Fix
npc.proxy_nearest_nest (on): the nearest living (not +0x190 extinct) nest herd of the species to the member's position. Off: the cached or first one.

## Verification
no-fail-run: manual. Before (02:25): the MSI's new nest (176 -473 -11) without members for minutes, the player in the host's nest. After (02:29): `coop.homes extinct 1` and `extinct 2` while connected: both rebuilt nests sent with a member and joined on their games ("home nest at (133.0 -486.5 -49.6)" on the MSI, "(129.8 -484.7 -80.3)" on the TITAN guest); 0 faults on all five games.

## History
- 2026-11-13: opened.
