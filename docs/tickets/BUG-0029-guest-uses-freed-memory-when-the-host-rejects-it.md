---
id: BUG-0029
title: guest uses freed memory when the host rejects it or leaves (REJECT / BYE)
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#7
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-core
commits: a702d15
tests: tests/net_tests.cpp test_session_end_in_delivery, make test-asan (in make test)
signature:
docs:
related: #7
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/7 (full text, discussion).

When a guest receives `MSG_REJECT` (version, build or "session full") or `MSG_BYE` (the host left), `Session::handle_msg` calls `leave()`. `leave()` destroys the Peer and its `Connection` while `Connection::on_packet` is still running on that same object. `on_packet` then reads and writes freed memory. This is a heap use-after-free on the guest (crash / memory corruption). Today it usually goes unnoticed because the freed memory still looks valid, but AddressSanitizer reports it on every rejected join, and the existing unit test `test_session` already triggers it.

### Where
- `Connection::on_packet`, the message loop that keeps using `this` after `deliver()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L204-L238
- Client `MSG_REJECT` handler calling `leave()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L418-L427
- Client `MSG_BYE` handler calling `leave()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L461-L466
- `leave()` frees every peer: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L77-L101
- `handle_packet` / `update()`: the role check only guards later `handle_msg` calls, not `on_packet` itself: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L473-L479 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L549-L551

### How to reproduce
A. The existing unit test, built with ASan (its "build mismatch must be rejected" part sends a REJECT):

```
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Imods/coop \
  -o net_tests_asan mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
./net_tests_asan
```

Output:

```
ERROR: AddressSanitizer: heap-use-after-free ... READ of size 2
  #0 net::Connection::on_packet(...) mods/coop/net/connection.cpp:223
  #1 net::Session::handle_packet(...) mods/coop/net/session.cpp:475
  #2 net::Session::update() mods/coop/net/session.cpp:549
  #4 test_session tests/net_tests.cpp:310
freed by thread T0 here:
  #10 net::Session::leave(char const*) mods/coop/net/session.cpp:94
  #11 net::Session::handle_msg(...) mods/coop/net/session.cpp:423
```

A second report follows in `std::_Rb_tree<...>::find` (the freed `rel_in_`).

B. BYE path (host leaves with an unacknowledged reliable message):

```cpp
#include <cstdio>
#include <thread>
#include <chrono>
#include "net/session.h"
using namespace net;
static void pump(Session& h, Session& c, int ms) {
    for (int i = 0; i < ms / 5; i++) { h.update(); c.update(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
}
int main() {
    std::string err;
    Session host, cli;
    host.host(0, "h", err);
    cli.join("127.0.0.1:" + std::to_string(host.port()), "g", err);
    pump(host, cli, 300);
    Writer w; w.u32(1);
    host.broadcast_reliable(MSG_NOTICE, w); // still unacknowledged when the host leaves
    host.leave("test");
    pump(host, cli, 100);
}
```

Built with the same ASan flags against `mods/coop/net/*.cpp`, it reports `heap-use-after-free ... in net::Connection::on_packet ... connection.cpp:220`.

In game: a guest with a different exe build or protocol, or a fourth guest on a full session, gets a REJECT. Or the host leaves (or quits) while traffic is flowing.

### Expected
`on_packet` never touches a destroyed connection. A REJECT or BYE ends the session cleanly after the packet is processed.

## Root cause
**What happens**

1. `Session::update()` calls `handle_packet(*p, ...)`. That calls `p.conn.on_packet(..., deliver)` with a lambda that forwards each message to `handle_msg(p, ...)`.
2. On the guest, `handle_msg` for `MSG_REJECT` or `MSG_BYE` calls `leave(...)`. `leave()` runs `peers_.clear()`, so the `unique_ptr<Peer>` that owns the `Connection` is deleted.
3. Control comes back into `Connection::on_packet`, whose `this` is now freed:
   - REJECT is a reliable message. Right after `deliver(type, pr)` the code does `next_rel_expected_++` (a write into freed memory) and then `rel_in_.find(next_rel_expected_)` on the destroyed `std::map` (connection.cpp lines 222-225).
   - BYE is unreliable (`continue` after deliver). But the host's `leave()` builds its last three packets with `build_packet(now + 1000, ...)`, so every pending reliable message is "due" and goes into the same packet after the BYE. The next loop iteration then reads `next_rel_expected_` from freed memory at line 220, and possibly writes it and walks the freed `rel_in_`.
4. The lambda's `if (role_ != Role::None)` check stops further `handle_msg` calls, and `update()` returns after `handle_packet`. Neither stops `on_packet` from touching its own members.

The host side is not affected: on the host, `MSG_BYE` only sets `p.bye`, and the peer is dropped later in `update()`.

### Confidence
CONFIRMED by harness (ASan, both paths). Not verified: whether the 32-bit Windows build actually crashes on players' PCs today. The freed block is usually still intact, so it may only corrupt the heap now and then.

## Fix
Fixed on fix/w1-core (a702d15). `Session::leave()` asked for while a Connection parses a packet
(`delivering_`, set around `on_packet` in `handle_packet`) only records the reason; `handle_packet`
runs the leave and then `cb.disconnected` after `on_packet` has returned. The REJECT and BYE handlers
store the disconnect text instead of calling the callback themselves. A `cb.custom` callback that
calls `leave()` is covered by the same path. No switch (memory safety). `make test` now also builds
and runs the unit tests with `-fsanitize=address,undefined` (`make test-asan`).

The review suggested:

Do not call `leave()` from inside message delivery on the client. Set a flag in `handle_msg` (like the host's `p.bye`), e.g. `pending_leave_ = "rejected"` plus the text, and call `leave()` plus `cb.disconnected` in `update()` after `handle_packet` returns. Alternatively, make `on_packet` return right after any `deliver()` that closed the connection, using a "closed" flag set by `Session` before `peers_.clear()`. That fix alone is not enough, though: `peers_.clear()` still frees the object before `on_packet` can read the flag, so deferring the leave is the safer option. This is a memory-safety fix, so there is no need to keep the old behaviour behind a `coop.fix` switch.

## Verification
Unit test, 2026-10-28: `test_session_end_in_delivery` (BYE with an unacknowledged reliable message
behind it, and a build REJECT; `cb.disconnected` exactly once with the reason, role None).
FAIL on main under ASan: heap-use-after-free in `Connection::on_packet` (already from the old
`test_session` REJECT part; the new BYE case alone also fails, built against main's net code).
PASS with the fix (`make test-asan`, `make test`).

The planned test was:

`tests/net_tests.cpp`: a case that joins, queues a reliable message on the host, calls `host.leave()` and pumps, plus the existing reject case. `make test` builds `net_tests` with `-fsanitize=address` (or a separate ASan target in `make test`), so the test fails on main with heap-use-after-free and passes with the fix. Also check that `cb.disconnected` was called exactly once with the reject reason.

### Related
- Same pattern risk: any future client-side handler (or `cb.custom` callback in coop.cpp) that calls `g_sess.leave()` while a message is being delivered.
- CLAUDE.md section 2 "Object lifetime" (most crashes were a freed object used later).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #7.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w1-core: deferred leave during delivery, ASan unit tests in make test.
- 2026-10-28: released in 0.7.3.0.
