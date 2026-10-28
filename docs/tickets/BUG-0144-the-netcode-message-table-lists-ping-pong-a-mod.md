---
id: BUG-0144
title: the netcode message table lists PING/PONG, a mod version and fields the protocol does not send
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#123
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: 815813e
tests: docs_lint (make test)
signature:
docs: docs/04-netcode.md 4.3
related: #123
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/123 (full text, discussion).

The message table in docs/04-netcode.md 4.3 describes fields and messages that do not exist on the wire. Nobody sends `MSG_PING`, so `MSG_PONG` never happens; RTT comes from the packet acks. `MSG_ABILITY` (11) has no sender and no handler. A reader who trusts the table (a fake peer, a test bot, a new message) builds against the wrong contract. Severity: minor (docs and protocol cleanup).

### Where
- Message ids: [mods/coop/net/protocol.h#L20-L31](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/protocol.h#L20-L31)
- `Hello` / `Welcome` / `PlayerJoin` as sent: [mods/coop/net/protocol.h#L94-L148](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/protocol.h#L94-L148)
- The only PING code, a host-side answer: [mods/coop/net/session.cpp#L368-L373](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L368-L373)
- RTT from acks: [mods/coop/net/connection.cpp#L133-L141](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L133-L141)
- The table: [docs/04-netcode.md#L48-L72](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/04-netcode.md#L48-L72)

### How to reproduce
`grep -rn "MSG_PING\|MSG_PONG\|MSG_ABILITY" mods tools tests` finds only protocol.h and the host's answer in session.cpp. Compare the table with the `write()` methods in protocol.h.

### Expected
The table matches the wire, and unused ids are either removed (kept reserved in a comment) or implemented.

## Root cause
**What happens**

| Table says | Code does |
|---|---|
| `HELLO`: mod version, exe build-id, nickname | `u16 proto, u32 build, str8 name`: no mod version |
| `WELCOME`: player id, world id, host tick, player list | `u8 id, u32 world, u8 n, {u8 id, str8 name}`: no host tick |
| `PLAYER_JOIN/LEAVE`: id, nickname, appearance | JOIN `u8 id, str8 name`; LEAVE `u8 id, u8 reason`: no appearance (it comes from `MSG_CREATION` and the state's species) |
| `PING/PONG`: timestamp, RTT in the status | no sender of `MSG_PING` anywhere (mod, tools, tests); RTT is the ack-time average `rtt_ms` |
| (not listed) `MSG_ABILITY = 11 // M2` | no sender, no handler |

The missing mod version matters beyond the docs. Peers of different mod versions with the same `kProtoVersion` (7, for all of 0.7.x) are accepted with no warning, so a message layout change without a protocol bump would be read wrongly without any error. So far the layout changes kept old readers working: optional trailing fields in `MSG_VOTE`, `MSG_LOBBY` and `MSG_CELL_EAT`.

### Confidence
CONFIRMED by reading (every writer and reader in mods/coop compared in the review).

## Fix
docs/04-netcode.md 4.3: the rows `HELLO`, `WELCOME`, `REJECT`, `PLAYER_JOIN/LEAVE` list the
fields the `write()` methods of net/protocol.h send; `PING/PONG` (8, 9) are marked unused (nothing
sends PING, the host still answers one; RTT is `rtt_ms` from the acks) and `ABILITY` (11) reserved;
a CORRECTION line under the table. protocol.h: comments on ids 8, 9 and 11. Verified 2026-10-28:
`grep MSG_PING|MSG_PONG|MSG_ABILITY` finds only protocol.h and the host's answer in session.cpp.
Not done (left for a protocol bump): a mod version in `HELLO`.

## Verification
Documentation; `make test` (docs_lint) passes.

### Related
#47 (another 04-netcode mismatch: timeouts), #12 (sizes of reliable messages).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #123.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
