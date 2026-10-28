---
id: BUG-0164
title: a long name from the cheat console is cut inside a character, and a log line with a non-ASCII path is truncated
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#143
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 116731e
tests: make test (tests/text_tests.cpp)
signature:
docs:
related: #143
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/143 (full text, discussion).

Three small text-handling gaps:

1. **No length cap for `coop name`.** The co-op window caps the name at 24 characters, but `coop name <nick>` (cheat console or devkit) takes any length. It only changes `g_name`: a session that is already running keeps the old name for the other players, while local tags and notices use the new one.
2. **`str8` cuts at 255 bytes, not at a character boundary.** Names, notices and other str8 texts longer than 255 bytes are cut inside a multi-byte UTF-8 character. The receiver then shows U+FFFD at the end. This is reachable with a long name from (1): 128 Cyrillic letters are 256 bytes.
3. **`%ls` in a log line.** `creations: My Creations %ls` formats a wide path with the C library's `%ls`, which converts with the "C" locale. A Documents path with non-ASCII letters (a Cyrillic Windows user name) truncates or drops the line. The log that problem reports rely on then lacks the creations path.

### Where
- `coop name` without a cap or session update: [coop.cpp#L1913-L1919](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1913-L1919); UI cap: [coop_ui.cpp#L410](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L410)
- Byte cut: [net/buffer.h#L25-L30](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/buffer.h#L25-L30); the name in Hello: [net/protocol.h#L128-L129](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/protocol.h#L128-L129)
- `%ls`: [creations.cpp#L161](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L161), formatted by `vsnprintf` in [sporekit/log.cpp#L64-L69](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/log.cpp#L64-L69)

### How to reproduce

### Expected
- Names are capped like in the UI (24 characters) wherever they are set, and a name change while connected is either refused or sent.
- `str8` / `str16` never cut inside a UTF-8 sequence.
- Log lines print paths as UTF-8.

## Root cause
**What happens / How to reproduce**

- Cheat console: `coop name` followed by 130 Cyrillic letters, then join a host. The host's lobby shows the name ending in a replacement character. While connected, `coop name X` changes your own tags to X while the others still see the old name.
- On a Windows account whose user folder has Cyrillic letters, start Spore with SporeCoop: the "creations: My Creations" line in sporekit.log is cut at the first non-ASCII letter (or missing).

### Confidence
CONFIRMED by reading, low severity. A second reviewer verified it.

## Fix
Fixed on fix/w3-sys (116731e). `net::Writer::str8` / `str16` cut a too long text between two
characters (`utf8::cut_bytes`, `mods/coop/net/utf8.h`). Names from `coop.name`, `coop.host`,
`coop.join`, the co-op window and coop.ini go through `utf8::player_name` (24 code points, no
control characters); `coop.name` is refused in a session (the others keep the session's name).
creations.cpp logs the My Creations path as UTF-8 with `%s` (and the `stack all` command too).
`coop.corefix text_utf8 0` = the byte cut.

## Verification
Test: `tests/text_tests.cpp` in `make test` (a 260-byte Cyrillic str8 and a 4500-byte CJK str16 read
back as valid UTF-8; names capped).
* FAIL with `text_tests --old` (the byte cut): 3 checks.
* PASS with the fix (`make test`). The name cap and the refusal in a session have no game test.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #143.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (116731e); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
