---
id: BUG-0129
title: chat in notifications is cut short for non-Latin text, styled wrongly, and cannot be read in full anywhere
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#108
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 116731e
tests: make test (tests/text_tests.cpp)
signature:
docs: docs/08-ui.md
related: #108
switch: core.notify_text
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/108 (full text, discussion).

Several small problems together mean a partner's chat message often cannot be read:

1. The notification panel wraps and cuts text by **bytes**, not characters (46 bytes per line, 2 lines). A Cyrillic message (2 bytes per letter) gets about half the room of an English one. A CJK message (3 bytes per character) is cut inside a UTF-8 sequence, and the last character shows as U+FFFD.
2. The panel style is guessed from English words in the text:
   - Chat arrives as `"<Bob> text"`, which has no `": "`, so chat is **never** styled as chat. It gets the info style and 6 s instead of 8 s.
   - A chat that contains words like "lost", "failed", "joined" or "left" gets the danger, join or leave style (danger stays 10 s).
   - System lines that contain `": "` get the chat style.
   - Russian notices (Russian UI) never match any keyword.
3. The co-op window shows only the **last 4** of the 50 kept chat lines, with no scrolling. Join/leave/stage notices go into the same list, so a message is pushed out within seconds. docs/08-ui.md says "last 5 lines" and "The full history stays in the F9 window chat", which is not true.

### Where
- Wrapping by bytes, cut at byte 46 / 43: [notify.cpp#L59-L77](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/notify.cpp#L59-L77)
- Style from keywords: [notify.cpp#L35-L52](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/notify.cpp#L35-L52)
- Chat text without ": ": [coop.cpp#L1725-L1728](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1725-L1728), into [feed, coop.cpp#L118-L125](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L118-L125)
- 4 lines of 50, no scroll: [coop_ui.cpp#L624-L627](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L624-L627), [coop.cpp#L137-L142](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L137-L142)
- Docs: [docs/08-ui.md#L22](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/08-ui.md#L22), [docs/08-ui.md#L102](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/08-ui.md#L102)

### How to reproduce
- Host and guest are connected. The guest sends a Russian chat line of about 60 letters (or a Chinese line of 30 characters).
- On the host the panel shows only the first part, and the CJK case ends in a replacement character. `coop.notify` (notify::status) prints the text. The panel uses the info style, not chat.
- Then let 4 notices arrive (a third player joins and leaves, a stage change): the message is no longer in the F9 window.

### Expected
- Wrap by characters (code points, ideally by measured width), and never cut inside a UTF-8 sequence.
- The kind of a notice is given by the code that creates it, not guessed from words in player text.
- The chat history can be scrolled in the F9 window (or at least the documented 5 lines, with chat kept apart from system notices).

## Root cause
**What happens**

`lines_of` was run on the host with the exact code from notify.cpp:

| Message | Characters | Shown | Note |
|---|---|---|---|
| `<Ivan>` plus a 52-letter Russian sentence | 58 | 46 (line 1 only 20) | rest replaced by "..." |
| Russian space notice "Offer sent: <name>: 5 <spice> x $1200" | 60 | 50 | |
| `<Li>` plus 30 Chinese characters | 34 | 36 incl. "..." | line 2 is invalid UTF-8, ends in U+FFFD |
| English, 78 characters | 78 | 77 | fine |

The same message in the co-op window is one of 4 lines and is gone after 4 more events. So a non-Latin message longer than about 23 characters cannot be read in full anywhere in the game.

### Confidence
CONFIRMED, low severity: a host-side harness ran on the real `lines_of`, the rest was confirmed by reading. A second reviewer verified it.

## Fix
Fixed on fix/w3-sys (116731e). The panel text is wrapped by letter cells (`notify_text.h`: code
points, a CJK character takes two cells; 46 cells per line, two lines), never inside a character.
`notify::show(text, kind)`: chat (`"chat"`), join and leave give their kind; other system texts are
still guessed from English words, but `": "` no longer makes a system line chat. The co-op window
wraps the chat history (50 lines) to 70 cells per line and scrolls it with two arrow buttons on the
right (`chatup` / `chatdown`). `coop.notify` status prints the whole text and the panel's lines.
docs/08-ui.md corrected. `coop.corefix notify_text 0` = the byte wrap and the old guesses. Left: the
Russian system notices are still guessed as "info" (each caller would need its kind).

## Verification
Test: `tests/text_tests.cpp` in `make test`: a 65-letter Russian chat line shows all of it in valid
UTF-8, a CJK line never ends inside a character, "Offer sent: Bob: 5 spice" is not chat, the chat
history wrap keeps every character.
* FAIL with `text_tests --old`: 3 checks (90 % of the Russian line, valid UTF-8, the chat guess).
* PASS with the fix (`make test`). Lab: `notify` (coop_notify.py) in run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842). The scroll arrows have no
game test.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #108.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (116731e); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: core.notify_text (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
