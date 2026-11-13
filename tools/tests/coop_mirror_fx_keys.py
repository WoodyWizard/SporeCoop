#!/usr/bin/env python3
"""BUG-1049: the stop of a keyed effect the host relayed to a guest's mirror goes out also when the host's table of
relayed keyed effects is full.

The host remembers each keyed effect start it relayed (so that the stop goes out once). The table (256) never forgot
the keys of creatures that died or of a world that was gone; once full, every new start overwrote slot 0 and that
key's stop was never sent: a looped effect stayed on the guest's mirror until the mirror went.

1. Two games on the tribe fixture. The host relays a start of a member's keyed pool effect (0x21C934A7 key 0x55559EF,
   pool 2, a behaviour's effect of the fight; `coop.mirrorfx key relay`: as the effect hook relays one, without the
   start in the host's game): the guest's game makes it on the member's mirror (`coop.mirrorfx has`: the game's
   effect of that key in the mirror's pools).
2. The host's table is filled as by a long session (`coop.mirrorfx fill 256`: starts of no creature), then the host
   stops the member's effect (`coop.mirrorfx key stop`).
3. PASS: within 5 s the guest's game gets the stop of that key for the mirror (`has ... stops=`: the stops the guest
   received and passed to the game's StopEffectFromPools).
No keyed effect of a fight stays on a mirror long enough to read its end (docs/re/tribe.md#tribe-cycle-restart: the
behaviour's effects end within 1 s, the sprint's dust is started and stopped by the mirror's own update), so the check
is the stop reaching the guest's game, not the effect's state after it.

  SPORE_FIX_OFF=mirror.fx_keys   FAILS (the stop of the evicted key is never sent)
"""
import re
import time

from coop_tribe_cycle_restart import members
from twoinst import Checks, start

EFFECT, KEY, POOL = 0x21C934A7, 0x55559EF, 2
ARGS = f'{EFFECT:#x} {KEY:#x} {POOL}'


def has(guest, mid):
    """(mirror, effect made, active, stops received) for the key on the member's mirror in the guest's game"""
    out = guest.call(f'coop.mirrorfx has {mid} {KEY:#x}')['out']
    m = re.search(r'mirror=(\d) effect=(\d) active=(\d) stops=(\d+)', out)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0, 0)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    mids = sorted(set(members(host)) & set(members(guest)))
    ck(bool(mids), f'a host member has its mirror on the guest ({mids[:5]})')
    if not mids:
        return ck.result()
    print('  host table before: ' + re.search(r'keys=\d+', host.call('coop.mirrorfx')['out']).group(0), flush=True)
    mid = None
    for m in mids[:6]:
        print(' ', host.call(f'coop.mirrorfx key relay {m} {ARGS}')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 2 and not has(guest, m)[1]:
            time.sleep(0.05)
        if has(guest, m)[1]:
            mid = m
            break
        host.call(f'coop.mirrorfx key stop {m} {ARGS}')
    if mid is None:
        print('  guest: ' + guest.call('coop.mirrorfx')['out'].strip(), flush=True)
        print('RESULT: SKIP (the guest\'s game made the relayed effect on no mirror)', flush=True)
        return 0
    s0 = has(guest, mid)[3]
    print(f'  member #{mid}: the guest\'s game made the effect on the mirror; stops so far {s0}', flush=True)
    print(' ', host.call('coop.mirrorfx fill 256')['out'].strip(), flush=True)
    print(' ', host.call(f'coop.mirrorfx key stop {mid} {ARGS}')['out'].strip(), flush=True)
    t0 = time.time()
    got = has(guest, mid)
    while time.time() - t0 < 5 and got[3] <= s0:
        time.sleep(0.5)
        got = has(guest, mid)
    print(f'  after the host\'s stop: {got}; host {host.call("coop.mirrorfx")["out"].strip()}', flush=True)
    ck(got[3] > s0, f'the guest\'s game got the stop of the mirror\'s effect ({s0} -> {got[3]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
