---
id: BUG-0108
title: 0.7.x is called the GOG 2016 release, but the 0.7.2.2-0.7.2.4 fixes are off on GOG 2016
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#87
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 342e273, ee9b778
tests: re_lint lists the frozen build's missing names (make test); docs by reading
signature:
docs:
related: #87
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/87 (full text, discussion).

README.md and CHANGELOG.md say 0.7.x is the release line for the old GOG 2016 build (`922af7c7`), and `install.bat` accepts that build as supported. Since the 2026-10-23 decision, new code only gets GOG 2024 symbols. The 0.7.2.2, 0.7.2.3 and 0.7.2.4 fixes use 13 symbols that have no GOG 2016 address, so on GOG 2016 those fixes switch themselves off without a word. The code handles this cleanly, as the policy intends. The defect is that no player-facing text says so: a GOG 2016 player who installs 0.7.2.4 reads "No more crash on Alt+Shift+Enter" and "a guest grows up again after a death", and still has both bugs.

`tools/re_lint.py` makes this harder to see: for the frozen build it prints only a count, not the missing names.

### Where
- README Compatibility table, GOG 2016 row and footnote 2: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/README.md#L120-L127
- CHANGELOG 0.7.0, "0.7.x is the last version for the old GOG 2016 build": https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CHANGELOG.md#L203-L205
- CHANGELOG 0.7.2.2-0.7.2.4 entries, no build caveat: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CHANGELOG.md#L5-L124
- The policy: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L8-L10
- install.bat accepts `922af7c7` as supported: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/package.sh#L25-L33
- Where the fixes turn off:
  - Alt+Shift+Enter guard: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/gamefixes.cpp#L168-L171
  - Hatchling grow-up (`grow_up` returns false without `cCreatureAnimal_GrowTo`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L99-L110
  - Cell bite, chunk, loot and spin hooks: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L904-L912
- re_lint prints only the count: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/re_lint.py#L48-L49

### How to reproduce
**How to reproduce / check**

1. `python3 tools/re_lint.py`: the frozen-build line above. To list the names, run `grep -oh 'H->sym("[^"]*")' mods/coop/*.cpp` and check each name against `data/symbols/*922af7c7*.sym`.
2. On a GOG 2016 lane, the game log shows "game fixes: Alt+Shift+Enter guard off".

### Expected
The player-facing text matches the policy. For example:
- the 0.7.2.2-0.7.2.4 CHANGELOG sections say "these fixes need the GOG 2024 or Steam build";
- the README footnote says 0.7.x after 0.7.2.1 gets no GOG 2016 fixes.

Otherwise, the 13 addresses are ported.

## Root cause
**What happens**

- The claims:
  - README: "² 0.7.x is the last version for the GOG 2016 build; from 0.8 only the current GOG build ..."
  - CHANGELOG 0.7.0: "**0.7.x is the last version for the old GOG 2016 build**".
  - CHANGELOG 0.7.2.4: "**No more crash on Alt+Shift+Enter**" and "**Creature: a guest grows up again after a death**". 0.7.2.2: "Cell: the host's creatures no longer spin on the guest's screen, and the guest can kill them".
- The reality: `make test` (re_lint) prints

  ```
  re_lint: 13 symbol(s) missing for the frozen build(s) 922af7c7 (not supported)
  ```

  The 13 symbols, which the code asks for and which exist only in `overrides_dc04aee5.sym`:
  - `SporeCoop::AppSystem_NextAdapter`, `SporeCoop::AppSystem_ToggleFullscreen`: BUG-0012, Alt+Shift+Enter.
  - `SporeCoop::cCreatureAnimal_GrowTo`, `SporeCoop::PosseSimulator_Get`, `SporeCoop::PosseSimulator_GrowMembers`: BUG-0014, the hatchling.
  - `SporeCoop::cPosseSimulator_Get`, `SporeCoop::cPosseSimulator_AddMember`: pack recruit.
  - `Simulator::Cell::Bite`, `EatChunkA`, `EatChunkB`, `SoftBodyStep`, `SpawnLoot`, `EatFood`: BUG-0001 and BUG-0002, cell spin, guest bites, meat.

  Every one is optional in the code. The fix just logs "off" (gamefixes) or skips the hook (cellsync), and `grow_up` returns false. On GOG 2016:
  - Alt+Shift+Enter still runs the game's crashing adapter switch.
  - A guest's hatchling still never grows up after a respawn.
  - The cell guest's lethal and chunk bites are not forwarded, and mirrors are not turned.

### Confidence
Confirmed by reading and by running re_lint. A second reviewer verified it, and classified it as a docs issue: the clean switch-off follows the CLAUDE.md policy.

## Fix
Decided by what the code does: the 14 symbols the newer fixes need have no GOG 2016 address and
each feature switches itself off there (the policy since 2026-10-23), so this is a docs correction,
not a port. README (Compatibility row and footnote 2) and the 0.7.2.2, 0.7.2.3 and 0.7.2.4
CHANGELOG sections now say which fixes are off on GOG 2016; [Unreleased] has a line. re_lint prints
the missing names, not only their count. The installer still accepts GOG 2016 without a word (left:
a message there is an installer change, wave 4 has an installer agent).

## Verification
`python3 tools/re_lint.py` prints "14 symbol(s) missing for the frozen build(s) 922af7c7 (not
supported, these features are off there): Simulator::Cell::Bite, ..." (main printed only the
count). README and CHANGELOG by reading.

### Related
#64 (re_lint gaps).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #87.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
