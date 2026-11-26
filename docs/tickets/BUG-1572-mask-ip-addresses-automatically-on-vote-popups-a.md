---
id: BUG-1572
title: Mask IP addresses automatically on vote popups and add streamer mode toggle
status: fixed
kind: security
stage: UI
reported: 2026-11-26
source: YouTube 0.8.0 stream audit
role: both
versions: 0.8.0
fixed_in: unreleased
branch: main
commits: fa4b4d07, 1e808fcb
tests: ui_privacy_i18n
signature:
docs: docs/08-ui.md
related: BUG-1575
---

## Report
During a live 0.8.0 gameplay stream, clicking Evolve or triggering an editor/stage-transition vote automatically popped open the F9 Co-op Lobby on both host and guest screens. Because the lobby always displayed the raw IPv4 address (`Friends connect to: <IP>:<port>` / `Host address: <IP>:<port>`), streamers accidentally leaked their host/VPN IP address on stream every time a vote popped up.

## Root cause
- Observed in `mods/coop/vote.cpp`, `mods/coop/session_glue.cpp`, and `mods/coop/coop_ui.cpp`:
  1. `start_vote()`, `start_action_vote()`, and guest vote/offer handlers called `coop_ui::open(true)`, which always rendered raw IP addresses in `D.address`, `D.host_hint`, LAN entries, and recent server entries.
  2. There was no local streamer/privacy toggle in `coop.ini` to keep IP addresses hidden when opening F9 manually during a stream.

## Fix
Implemented under switch `ui.hide_ip` (`coop.fix ui.hide_ip off` restores unmasked IP display for testing):
- Added `i18n::mask_ip_address()` in `mods/coop/i18n.h` to mask host/IPv4/IPv6/comma-separated addresses as `***.***.***.***:<port>` while preserving the UDP port.
- Added `coop_ui::open_for_vote()` in `mods/coop/coop_ui.h` / `mods/coop/coop_ui.cpp`, setting transient `g_vote_auto_open = true` whenever a vote or editor offer auto-opens F9 (cleared on close or manual F9 toggle).
- Routed all vote/offer auto-open sites in `mods/coop/vote.cpp` and `mods/coop/session_glue.cpp` through `coop_ui::open_for_vote()`.
- Added persistent local setting `Hide IP (streamer mode)` (`coop.ini [ui] hide_ip`, Settings page 4 `Display (this PC)`) and console command `coop.uistate hide_ip 0|1` (`mods/coop/coop_cmds.cpp`), masking `D.address`, `D.host_hint`, LAN entries, and recent servers while keeping real join addresses intact for clicks.

## Verification
- Unit tests (`make unit-cpp`, `tests/validate_tests.cpp::test_i18n_and_privacy`): PASS (`mask_ip_address` across IPv4, multi-NIC comma list, hostname, `[IPv6]:port`, no-port, empty).
- Wine lab ON/OFF twin (`out/lab/20261126-132121`):
  - `ui_privacy_i18n` (ON): **PASS** (34 s, 0 faults; verified raw IP shown manually with `hide_ip=0`, masked as `***.***.***.***:<port>` on host & guest when `hide_ip=1`, auto-masked on BOTH host & guest during `coop.vote test` with `vote_auto=1` even while `hide_ip=0`, and restored on manual F9 reopen).
  - `ui_privacy_i18n_off` (`SPORE_FIX_OFF=ui.hide_ip`): **FAIL** (33 s; failed all 4 host/guest IP masking assertions for both `hide_ip=1` and vote auto-popups).
- Regressions: `ui_golden` **PASS** (`out/lab/20261126-132121`, 87 s), `ui` **PASS** (`out/lab/20261126-133040`, 108 s).

## History
- 2026-11-26: opened from 0.8.0 YouTube stream audit.
- 2026-11-26: fixed on `main` (`fa4b4d07`, `1e808fcb`), verified with unit tests and ON/OFF Wine lab twin (`out/lab/20261126-132121`).
