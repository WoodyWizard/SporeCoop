---
id: BUG-1111
title: Space: the captain screen crashes the game at 0xA055B5 when the player's empire has no captain (captain key 0): the portrait's animated creature is null
status: fixed
kind: crash
stage: space
reported: 2026-11-14
source: GitHub issue #202
role: guest
versions: 0.7.13
fixed_in: unreleased
branch: fix/w50-captain
commits: 6a0cfbf0 3aaba7d3 4efe5f69 6dfc9c94
tests: space_captain_none
signature: 3010a413:SporeApp.exe SporeCoop::AnimatedCreature_SetEpicFlag+0x9B5 (0xC0000005 at 0xA055B5, write of 0x54; now SporeCoop::AnimWorld_vt40+0x15), calls 00AE8D88 <- 01072C75 (SpaceGameUI_ShowCaptainScreen) <- 0107367D (rank badge handler)
docs: docs/re/space.md#space-captain-portrait-null, docs/known-issues.md#issue-captain-uncopyable-species, docs/06-testing.md#testing-captain-none
related: BUG-0304, BUG-0303
---

## Report
GitHub #202, SporeCoop 0.7.13, Steam exe (the GOG 2024 code), Windows 10, Space: "crash when the captain button was
pressed at the beginning of Space", both players. The zip is the guest's: a session in the host's Space world (loaded
from the host's share 6.5 min before), the ship had landed on a planet 0.6 s before; one fault in the whole session,
the crash: 0xC0000005 at 0xA055B5 (SporeApp+0xA055B5 numbered from 0x400000), a write of 0x54, the game's main thread,
from a UI click (the window manager -> the rank badge handler of the Space UI).

## Root cause
Observed (disassembly of GOG 2024, the reporter's minidump, runtime probes on lane 2):
* The Space captain screen (0x1072810) copies the player empire's captain key (cEmpire +0xC4) first and, at its end,
  shows the captain's portrait: SporeCoop::CreatureDisplay_Show 0xAE8C80 -> Anim::IAnimWorld::LoadCreature 0xA0B1C0,
  which returns null when nothing can be loaded; 0xAE8C80 passes the null creature to the anim world's vt 0x40
  (0xA055A0), which writes creature+0x54: the crash. The dump's copy of the captain key (the caller's local passed as
  the key) is 0/0/0.
* A Space save without a captain makes the game ask "Name Your Captain" after the load (0xFE0F30 -> 0xE1DC50); its
  check button copies the world's player species into a new named creature and sets it as the captain. When the
  species cannot be copied, the game sets an all-zero captain and does not ask again in that load (lab: the species
  manager pointed at an unknown species).
* With captain 0 the screen's portrait cannot load. The lab reproduces the crash with the screen opened by the
  game's Space update path (0xFDF5A0 -> 0x1072810(0, 0) on a fresh UI: the Galactic Adventures mission screen with the
  captain's portrait): the guest crashes at 0xA055B5 with the reporter's call chain.
Inferred / not known:
* That the reporter's captain was 0 because "Name Your Captain" could not copy the world's species (the host's
  creation) on the guest: the log has no line on the species or the captain. A host creation the mod registered on
  the guest copies fine in the lab.
* In the lab the rank badge path of a captain-less empire stops at a handled game fault in the captain screen's
  equipment panel (0x5971D4 or 0xC02A14) before the portrait; why the reporter's badge click did not is not known.
* "Both" players crashing fits a world that had no captain (both games show its captain screen); only the guest's
  report is in hand. coop does not write the captain key (no code touches cEmpire +0xC4).

## Fix
gamefixes.cpp `game.captain_portrait`: a hook on SporeCoop::CreatureDisplay_Show gives a portrait whose creature this
game cannot load (no key and no name, or a key no database of the game has) the world's player species
(cSpeciesManager +0xA4) when the game has it, else skips the portrait (the next step 0xAE7D90 checks its slot). A
warning names the key. `coop.fix game.captain_portrait off`: the game's code (0.7.14). `coop.captain` prints the
captain and the species; tests open the screen as the Space update does and emulate an uncopyable species.
New symbols (overrides_dc04aee5.sym): CreatureDisplay_Show, AnimWorld_vt40, SpaceGameUI_Get,
SpaceGameUI_ShowCaptainScreen, cEmpire_GetCaptainKey / SetCaptainKey, NameCaptainDialog_Show / OnButton.

## Verification
* FAIL: lab 20261114-101858-fix-w50-captain (`SPORE_LAB_FIXOFF="space_captain_none=game.captain_portrait"`): the
  guest crashed at 0x00A055B5 (write of 0x54), calls 00AE8D88 <- 01072C75, as the reporter.
* PASS: lab 20261114-102630-fix-w50-captain (2 of 2, lanes 2 and 3; the same test with its post-crash checks adjusted to the mission screen this path shows). Related: space_captain and space_coop pass (lab 20261114-102807-fix-w50-captain); make test passes.
* 0.7.14 (main a762f240) has no change in this code path: the reporter's crash would happen there too.

## History
- 2026-11-14: opened from GitHub #202 (0.7.13); root cause at the portrait (captain key 0), "Name Your Captain"
  failing on an uncopyable species found; guard game.captain_portrait, test space_captain_none (fix/w50-captain).
