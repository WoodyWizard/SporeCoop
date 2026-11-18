---
id: BUG-9137
title: Tribe: the partner's village is founded with the host's creature the partner played a moment before
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-11
source: owner report on 0.7.8-lineages53-dev ("Tested Tribal with one other player, and it didn't work. I switched worlds when Kittykath left.")
role: host
versions: 0.7.8-lineages46-dev to 0.7.8-lineages53-dev
fixed_in: 0.7.8-lineages54-dev
branch: feat/lineages-47
commits:
tests: QA DuoQAJiffHost (a copy of the owner's Jiff Peanutbutter.spo and EditorSaves, the broken roster) + DuoQACreatureGuest with its own creature; host log "tribe: founder repair (BUG-9137)"; coop.fix tribe.founder_settle off / on
signature: host log "tribe automatic founders: Both species have their own villages" a few seconds after the Tribe arrival, then "creations: player 1 creature = <another key>" right after; the roster's guest species is one of the host's own creations
docs: -
related: BUG-9134 BUG-9135
switch: tribe.founder_settle
---

## Report
Host log 2026-11-11: BigTexas rejoined while the host was in its Creature world (Vandh) and reported the creature he
played there, the host's own 291EE5B5 'Clop', sent by key without records. The host then loaded Jiff Peanutbutter
(Tribe) at 21:30:11. The automatic founders ran at 21:30:20 with that key and founded a village of Clop; BigTexas's own
creature for this world, 7C67F1C3 'Squart', arrived at 21:30:22. The saved roster (`.duofounders` DUO1) binds the guest
seat to 291EE5B5. BigTexas commands nothing: village_commander() looks for a player whose creature is the seat's founder
model.

## Cause
auto_founders_tick took the partner's creature key at the first try after the arrival (5 s). Nothing waited for the
partner's creature of the new world, and nothing refused a creature that is the host's own creation.

## Fix
tribe.cpp / creations.cpp, switch `tribe.founder_settle`:
* The founding waits 15 s after the world was entered and until the partner's creature has not changed for 10 s
  (creations::player_key_changed_at), and refuses one of the host's own creations sent without records
  (creations::plays_host_creation: "Your partner plays one of your creatures here ...").
* A village already founded with one of the host's own creations (roster guest species in the host's My Creations, no
  outfit yet) gets the partner's settled own creature as an outfit, on the host, once: archive_guest_model,
  record_outfit(seat 1), the native species profile change of the village (apply_guest_model, shared with the editor
  outfit), checkpoint reload. Log "tribe: founder repair (BUG-9137)".

## Test (2026-11-11)
* QA one PC (copy of the owner's Jiff Peanutbutter.spo + EditorSaves in profile DuoQAJiffHost, QA guest with its own
  Duckulon): switch off: commander=254, founder_guest=291EE5B5 (the broken state); on: "founder repair (BUG-9137): ...
  291EE5B5, now 7C0B35F6" 12 s after the guest's creature arrived, checkpoint reload, commander=1. A new founding with
  the guest playing the host's 291953FB (no records): on "Your partner plays one of your creatures here ..."; off it
  went on to the site search ("No clear, level founding site ..."), i.e. it would have founded with the host's creature.
* Two PCs: a fresh copy of the same save hosted on TITAN, the MSI as the partner (Duckulon): repair 11 s after its
  creature arrived, commander=1 after the reload; the MSI's game shows its own village food.
