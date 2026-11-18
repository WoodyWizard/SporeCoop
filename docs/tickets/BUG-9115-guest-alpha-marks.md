---
id: BUG-9115
title: a guest does not see the host's nest alphas as alphas (and sees other creatures as alphas)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-10
source: owner two-PC session on 0.7.3.6-lineages45-dev ("Creatures aren't marked as Alpha for player B")
role: guest
versions: 0.7.3.6-lineages45-dev
fixed_in:
branch: feat/lineages-45
commits:
tests: QA host 9 + guest 8, Samuth.spo (Creature), alpha bit compare per NPC id, see Verification
signature: proxy +0xB58 bit 0x1 differs from the host NPC's
docs: docs/re/creature.md
related: BUG-0118 BUG-0179
switch: npc.alpha
---

## Report
In a two-PC Creature session the guest's creatures were not marked as Alpha.

## Root cause
The alpha is `+0xB58` bit 0x1 of the NPC (its card reads "Alpha <name>", SocialPropose 0xDA43D0 adds a
round; docs/re/creature.md 8.3). The host's NPC state carried dead, family, impressed and scaled-health
flags only; a guest's proxy kept whatever bit its own herd gave it.

## Fix
`NF_ALPHA` (16) in the NPC spawn and state flags; the guest sets or clears bit 0x1 of the proxy with
every state. `coop.fix npc.alpha off`: the proxy keeps its own bit.

## Verification
Windows QA, host 9 + guest 8 in the host's Creature world (Samuth.spo), 10 fps; the bit read with
`mem.read` on both games per NPC id.
* 45 build: host alphas 6, 4 of 15 proxies differed (three host alphas plain on the guest, one plain
  creature an alpha on the guest).
* Fix: host alphas 7, 14 of 14 proxies equal. 0 faults.
* Not checked on screen: the card text.

## History
- 2026-11-10: reported, fixed on feat/lineages-45.
