---
id: BUG-0109
title: with 3+ players a departed player's last state arrives after PLAYER_LEAVE and leaves a ghost partner
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#88
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-net
commits: a8341cf, 9afc74d
tests: tests/net_tests.cpp test_state_after_leave
signature:
docs: docs/04-netcode.md#netcode-w2, docs/06-testing.md#testing-net-w2
related: #88
switch: net.state_roster
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/88 (full text, discussion).

With 3 or more players, the host relays a leaving guest's last `MSG_PLAYER_STATE` to the other guests. That relay is unreliable and queued behind the frame's snapshot chunks, so it reaches them one packet after the reliable `MSG_PLAYER_LEAVE`. The guest then recreates the departed player's `g_remotes` entry, which stays for the rest of the session. Results:
- a frozen puppet of the departed player is shown for about 5 s;
- in Space the ghost counts as a partner next to you, so a lethal hit only disables your ship and the ghost can "repair" it;
- the next player who gets that id inherits the ghost's interpolation buffer and can stay invisible for that guest.

Severity: wrong behaviour / desync (medium).

### Where
- Host relay of a guest's state, unreliable and not `first`: [mods/coop/net/session.cpp#L342-L355](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L342-L355)
- `drop_peer` broadcasts the reliable LEAVE in the same `update()`: [mods/coop/net/session.cpp#L272-L290](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L272-L290), [L569-L579](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L569-L579)
- The guest's `leave()` sends BYE at once, right after its last state: [mods/coop/net/session.cpp#L77-L101](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L77-L101)
- Packing: the unreliable loop stops at the first message that does not fit, while a small reliable message still joins the packet: [mods/coop/net/connection.cpp#L84-L121](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L84-L121)
- The client accepts a state for any id, without checking the roster: [mods/coop/net/session.cpp#L449-L454](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L449-L454)
- `player_state` creates the entry again: [mods/coop/coop.cpp#L1718-L1724](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1718-L1724)
- `player_join` reuses the entry and never clears `buf`: [mods/coop/coop.cpp#L1508-L1516](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1508-L1516)
- The interpolation buffer drops any state whose sender time is not newer: [mods/coop/net/interp.h#L48-L62](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/interp.h#L48-L62)
- The Space partner list has no age check: [mods/coop/coop.cpp#L2908-L2919](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2908-L2919)
- Its users: [mods/coop/spacecoop.cpp#L145-L160](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L145-L160) (rescue instead of death) and [L214-L224](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L214-L224) (repair by a partner close by)

### How to reproduce
Harness on the real `net::Connection` (build from the repo root with `g++ -std=c++20 -Imods/coop leave_order.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp`):

```cpp
#include <cstdio>
#include <vector>
#include "net/connection.h"
#include "net/protocol.h"
using namespace net;
int main() {
    Connection host, guest;
    host.conn_id = guest.conn_id = 42;
    uint32_t now = 1000;
    Writer chunk; for (int i = 0; i < 990; i++) chunk.u8(0); // one snapshot chunk
    host.send_unreliable(MSG_CELL_STATE, chunk);
    host.send_unreliable(MSG_CELL_STATE, chunk);
    PlayerState st; st.id = 2; Writer ws; st.write(ws);
    host.send_unreliable(MSG_PLAYER_STATE, ws);   // relay of B's last state
    PlayerLeave pl{2, 0}; Writer wl; pl.write(wl);
    host.send_reliable(MSG_PLAYER_LEAVE, wl);     // drop_peer(B) in the same update()
    std::vector<uint8_t> pkt; int k = 0;
    while (host.build_packet(now, pkt) && k < 5) {
        k++;
        guest.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader&) {
            printf("packet %d: %s\n", k, t == MSG_PLAYER_STATE ? "PLAYER_STATE(B)"
                                       : t == MSG_PLAYER_LEAVE ? "PLAYER_LEAVE(B)" : "snapshot chunk");
        });
    }
}
```

Output:

```
packet 1: snapshot chunk
packet 1: PLAYER_LEAVE(B)
packet 2: snapshot chunk
packet 2: PLAYER_STATE(B)
```

In the game (3 lanes):
1. Host, B and C in the Cell stage near more than 14 cells (two cell chunks per frame), or on a grown Civ planet.
2. B runs `coop.leave`.
3. On C, the feed says "B left" and then "player2 appeared in the world", and `coop.status` still lists a remote with id 2.
4. Space variant: C next to B's last position takes a lethal hit and gets "your ship is disabled" although nobody is there.
5. Identity variant: a player on a PC with a shorter uptime than B's joins and gets id 2. C never shows that player.

### Expected
After `MSG_PLAYER_LEAVE` no state of that player is applied, and a new player under the same id starts with an empty buffer.

## Root cause
**What happens**

1. Host H, guests B (id 2) and C. Guest B leaves (`coop.leave`, the window's Leave button, quitting). B's last frame sends its state, then `leave()` sends BYE three times in the same millisecond.
2. In one `Session::update()` the host receives both:
   - B's state is relayed to C with `send_unreliable_to`. It is appended after everything already queued for C, which includes the snapshot chunks the modules queued in the previous frame (each about 900 to 1000 bytes: cell pool, civ, tribe, UFO, NPC batches).
   - BYE sets `p.bye`, and the timeout loop of the same `update()` calls `drop_peer`, which queues the reliable `MSG_PLAYER_LEAVE`.
3. `flush()`, then `build_packet`:
   - Packet 1 gets the first chunk. The second chunk does not fit (`break`), so the relayed state behind it is not reached in this packet.
   - The small reliable LEAVE still fits and is added to packet 1.
   - The relayed state goes out in packet 2.
4. C handles packet 1: it erases B from the roster and `g_remotes` and despawns B's puppet. Then packet 2: `cb.player_state(B)` runs `g_remotes[2]`, which creates a new entry named "player2", and pushes the state.
5. `drive_puppet` spawns a puppet ("player2 appeared in the world"). It is hidden about 5 s later (`age < 5.0`), but the entry stays until the session ends.
6. Space: `spc.partners` lists every remote whose last state says Space, with no age check. The ghost is a partner "here":
   - `filter_own_damage` disables C's ship instead of letting the lethal hit kill it, so C waits 20 s for a rescuer who left;
   - if the ghost's last position is close, `down_tick` counts it as `close_by` and "repairs" the ship.
   - Cell and Tribe are not affected: their `partners()` check the age.
7. A new player joins and gets id 2 again (`alloc_id` takes the lowest free id). `player_join` reuses the ghost entry with B's buffer. The new player's states carry its own `now_ms()` (milliseconds since its PC booted). If that is lower than B's last `time_ms`, `push` drops every state as "old or duplicate". C never sees the new player's puppet for the whole session; the host and the new player see each other normally.

### Confidence
CONFIRMED by harness for the delivery order (rebuilt independently by a verifier on the real `net::Connection`, same result). The common trigger was confirmed by reading: BYE goes out right after the last state, and the relay and `drop_peer` happen in the same `update()`. The consequences on C (ghost entry, Space partner list, inherited buffer) were confirmed by reading and not run in the game.

## Fix
Fixed on fix/w2-net. `Session::handle_msg` (client) ignores a `MSG_PLAYER_STATE` whose id is not
in its roster (a8341cf), so the late relay of a departed player's state no longer recreates its
`g_remotes` entry. `cb.player_join` in coop.cpp (9afc74d) despawns and drops an existing entry of
the joining id, so a newcomer never inherits a ghost's snapshot buffer. The Space partner list
(`spc.partners`) still has no age check of its own; with no ghost entry it no longer lists one.
Switches: `coop.corefix state_roster 0`, `coop.corefix id_reuse 0`.

The review's suggestions were:

- In `Session::handle_msg` (client), ignore `MSG_PLAYER_STATE` whose id is not in `roster_`.
- In `player_join`, clear `r.buf` (and the drive state) of a reused entry.
- Optional: send the relay with `first = true`, or flush unreliable relays before a LEAVE of the same peer.

## Verification
Unit test `test_state_after_leave` (three real Sessions over localhost): the host queues two
~990-byte chunks for C, then B sends its state and leaves; B's state and BYE reach the host in one
update. `net_tests --nofix`: C gets 1 state of B after PLAYER_LEAVE (FAIL, the ticket's order
reproduced on the real sockets); with the fix 0 (PASS, 3 of 3 runs). Not run with 3 games (the
consequence in the game follows from the entry no longer being created).

Planned in the review:

- `tests/net_tests.cpp`: a two-session test (host plus a client with a known roster) where a state for a departed id arrives after LEAVE, asserting that `cb.player_state` is not called. It FAILS on main and PASSES with the roster check.
- In the game, `coop_*` with 3 instances: B leaves while the host is in Cell with more than 14 cells near C. Assert on C that no puppet of B's id exists 1 s later. Then a new instance joins as id 2: assert C shows its puppet at the game's position.

### Related
#54 (a new player shown with the previous id holder's look; this ghost buffer may be part of it), #31 (sender clock in the buffer), #10 (unreliable snapshots fill the flush). A separate issue from the 3-4 player review covers `spc.partners` not filtering stale or downed partners; that fix (an age check in `spc.partners`) also removes the Space effect here.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #88.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-net (wave 2, netcode); tests above FAIL without the fix and PASS with it.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: net.state_roster (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
