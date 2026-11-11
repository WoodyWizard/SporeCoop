---
id: BUG-0897
title: A guest with a name that is not in Latin letters is called Player in the host's notices (suspected, need info)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: owner, watching a player's recording 2026-11-10
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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

## History
- 2026-11-10: opened from the owner's observation; no logs for the session.
