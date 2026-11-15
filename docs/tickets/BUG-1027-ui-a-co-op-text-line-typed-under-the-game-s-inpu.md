---
id: BUG-1027
title: UI: a co-op text line typed under the game's input lock counts UTF-8 bytes against the character limit, takes DEL and breaks surrogate pairs
status: fixed
kind: bug
stage: UI
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-21
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: text_lines, tests/text_tests.cpp
signature:
docs: docs/known-issues.md#issue-locked-edit-utf8, docs/06-testing.md#testing-p2-ui
related:
switch: core.ui_locked_edit_utf16
---

## Report
Audit 2026-11-12 for 0.7.14, finding SPC-21. Not seen at runtime, found by code reading. When the player types into a
co-op text line while the game holds the input (a caption, the "game saved" box: the mod types for it,
BUG-0273 / BUG-0551), a name or chat line in Cyrillic, Greek, CJK stops at half (or a third) of its limit (a
24-character name at 12 Cyrillic letters, the 120-character chat at 60), DEL (0x7F) goes into the text, and an emoji
or other character outside the BMP becomes two replacement characters.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/coop_ui.cpp:542 locked_edit_apply keeps the text as
UTF-8 (`t`, :550) and :566 compares `(int)t.size()` (bytes) with max_len (the edit's limit in UTF-16 characters, set
by add_edit :521; name 24 :884, address 64 :952, chat 120 :1045). :566 `c >= 0x20` lets 0x7F through. :568..:572
encode each UTF-16 unit on its own: a surrogate pair becomes two 3-byte sequences (CESU-8), which utf16() turns into
U+FFFD. The normal typing path (the game's own edit with the focus) is not affected.
Player path: yes, typing in the co-op window while a caption or modal game box holds the input. Severity: Low.

## Fix idea
Work on the UTF-16 text (sui::edit_text as u16 or convert once): count UTF-16 units against max_len, skip
c < 0x20 and c == 0x7F, join a high and a low surrogate before encoding (or keep the u16string and set it
directly). Switch: `core.ui_locked_edit_utf16 off`.

## How to test
tools/tests/coop_chat_caption.py (the caption lock): type 30 Cyrillic letters plus an emoji (WM_CHAR with a
surrogate pair) into the chat line; assert the game's edit text has all 30 letters and the emoji and no U+FFFD.
FAIL on main (cut, U+FFFD), PASS with the fix. A name variant: 20 Cyrillic letters into the name line, all kept.

## Fix
New portable mods/coop/typed_text.h: apply() works on the UTF-16 text (the edit's limit is in UTF-16 units),
drops controls and DEL, takes a surrogate pair whole (or not at all at the limit), Backspace removes a whole pair;
coop_ui.cpp locked_edit_apply calls it. apply_old() is the 0.7.13 code. Switch `coop.fix core.ui_locked_edit_utf16 off`
(text_tests --old).

## Verification
coop_text_lines.py: 70 Cyrillic letters, DEL and an emoji typed under CRG_ClaimNest's input lock; the game's
text field must hold exactly the 70 letters and the emoji. tests/text_tests.cpp test_locked_typing (`--old` fails 4 checks).
* FAIL (SPORE_FIX_OFF=core.ui_locked_edit_utf16,...): out/lab/20261113-032307 (60 letters, no emoji, DEL in the text)
* PASS: out/lab/20261113-032115-2, out/lab/20261113-032426

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
- 2026-11-14: released in 0.7.14.
