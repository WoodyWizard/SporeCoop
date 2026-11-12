---
id: BUG-0897
title: A guest with a name that is not in Latin letters is called Player in the host's notices (suspected, need info)
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-10
source: owner, watching a player's recording 2026-11-10
role: guest
versions: 
fixed_in:
branch: fix/w48-reports
commits:
tests: name_nonlatin
signature:
docs: docs/06-testing.md#testing-frame-cap
related: BUG-0896, BUG-0142, BUG-0245, BUG-0774
---

## Report
In the recording of BUG-0896 the host's notice said "+4 DNA from Player's hunt" while, by the owner, the guest had a
nickname that is not in English letters. No logs exist for that session; the version is unknown.

## Root cause
Not known (inferred from the code, low confidence). The notice takes `g_sess.name_of(player)` (mods/coop/coop.cpp:214,
dna_ledger.h:133): the host's roster had the guest as "Player", the default of `[coop] name` (coop.cpp:137, 5970).
Ways a typed name can end up as the default or get lost:
* The name was only the player's nickname elsewhere (Discord, Steam) and was never typed in the co-op window: then
  nothing is wrong; the 0.7.12 window asks for the name at the first start (F1), which removes the doubt.
* `apply_name()` (coop_ui.cpp:576-582) saves the edit field's text only when a button that calls it is pressed and
  only when `sui::edit_text` returns a non-empty string: if the game's edit control gives back an empty or
  unconverted string for Cyrillic / CJK input (an IME, a layout the control does not take), the name stays "Player".
* The name written into coop.ini with an editor in a legacy code page (cp1251): SporeKit converts ini files
  (BUG-0142 iniutf, BUG-0245 iniencode); a wrong guess or invalid UTF-8 can leave the default.
* `utf8::player_name` (net/utf8.h:107) keeps bytes >= 0x20, so valid UTF-8 passes; it does not validate UTF-8.

## Fix idea
After the test shows which path loses the name: fix that path; in any case log the name a player joins with on both
games (escaped), and show the player its own name as the other games will see it (the lobby does).

## How to test
Two games; the guest gets a Cyrillic name three ways, one run each: typed into the window's name field with real key
events (SporeKit input, a Cyrillic layout), written into coop.ini as UTF-8, and written as cp1251. On the host:
the lobby row (`coop.uistate`), `coop.players`, and the text of a DNA notice after the guest's kill must show that
name, not "Player" or "guestN". Must fail on the path that is broken.

## Verification (2026-11-12, fix/w48-reports)
Test `name_nonlatin` (coop_name_nonlatin.py, lane 5): the guest gets a Cyrillic name three ways and joins the host
with it; on the host the session's player list, the lobby row and the join notice ("<name> joined the pack", the
same `name_of` the DNA notices use) name it so, never "Player":
* typed into the co-op window's name step (WM_CHAR, as the game's text input; the field holds 'Иван Петров', Continue
  saves it): the host shows 'Иван Петров';
* coop.ini written as UTF-8 before the games start (SporeKit converts it at the mod's start): 'Пётр';
* coop.ini as cp1251: read in the system's ANSI code page (Wine 1252 in the lab: 'Îëüãà'; a Russian Windows reads
  cp1251 as the name); not lost.
PASS: lab 20261112-050545-2. No path loses a non-Latin name. Side findings (no player impact): a hand edit of
coop.ini while the game runs is read wrongly once SporeKit converted the file in that run (it remembers the
encoding per run: edit with the game closed); a closed co-op window keeps the lobby rows of its last refresh.
Conclusion: the guest of the recording most likely never typed a name (before 0.7.12 the window did not ask for one
at the first start; 0.7.12 does, BUG-0765), so the host showed the default "Player". Closed as not reproduced; reopen
with a report archive of such a session (its log lines `player N '<name>' joined`).

## History
- 2026-11-10: opened from the owner's observation; no logs for the session.
- 2026-11-12: test name_nonlatin (typed, coop.ini UTF-8, cp1251): the name reaches the host every way; not reproduced (fix/w48-reports).
