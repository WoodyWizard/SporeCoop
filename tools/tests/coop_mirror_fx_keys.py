#!/usr/bin/env python3
"""BUG-1049: a looped effect the host relayed to a guest's mirror is stopped there also when the host's table of
relayed keyed effects is full.

The host remembers each keyed effect start it relayed (so that the stop goes out once). The table (256) never forgot
the keys of creatures that died or of a world that was gone; once full, every new start overwrote slot 0 and that
key's stop was never sent: the guest's mirror kept the looped effect until the mirror went.

1. Two games on the tribe fixture. The host's game starts a looped pool effect on one of its members (0x21C934A7 key
   0x55559EF, a pool-2 effect of a member's behaviour, `coop.mirrorfx key start`, relayed as the effect hook relays one): the guest's game has
   it active on the member's mirror (`coop.mirrorfx has`: the game's effect of that key in the mirror's pools).
2. The host's table is filled as by a long session (`coop.mirrorfx fill 256`: starts of no creature), then the host's
   game stops the member's effect (`coop.mirrorfx key stop`).
3. PASS: within 5 s the guest's game has no active effect of that key on the mirror.

  SPORE_FIX_OFF=mirror.fx_keys   FAILS (the stop of the evicted key is never sent: the effect stays active)
"""
import re
import time

from coop_tribe_cycle_restart import members
from twoinst import Checks, start

# a pool-2 effect of a member's behaviour (docs/re/tribe.md#tribe-mirror-fx): no Update of the mirror stops it
EFFECT, KEY, POOL = 0x21C934A7, 0x55559EF, 2
ARGS = f'{EFFECT:#x} {KEY:#x} {POOL}'


def active(guest, mid):
    out = guest.call(f'coop.mirrorfx has {mid} {KEY:#x}')['out']
    m = re.search(r'mirror=(\d) effect=(\d) active=(\d)', out)
    return (int(m.group(1)), int(m.group(3))) if m else (0, 0)


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
        print(' ', host.call(f'coop.mirrorfx key start {m} {ARGS}')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 4 and not active(guest, m)[1]:
            time.sleep(0.3)
        if active(guest, m)[1]:
            mid = m
            break
        host.call(f'coop.mirrorfx key stop {m} {ARGS}')
    if mid is None:
        print('  guest: ' + guest.call('coop.mirrorfx')['out'].strip(), flush=True)
        print('RESULT: SKIP (the guest\'s game made the relayed effect active on no mirror)', flush=True)
        return 0
    print(f'  member #{mid}: the effect is active on the guest\'s mirror', flush=True)
    print(' ', host.call('coop.mirrorfx fill 256')['out'].strip(), flush=True)
    print(' ', host.call(f'coop.mirrorfx key stop {mid} {ARGS}')['out'].strip(), flush=True)
    t0 = time.time()
    on = 1
    while time.time() - t0 < 5:
        time.sleep(0.5)
        on = active(guest, mid)[1]
        if not on:
            break
    print(f'  after the host\'s stop: active={on}; host {host.call("coop.mirrorfx")["out"].strip()}', flush=True)
    ck(not on, 'the guest\'s game stopped the looped effect on the mirror')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
