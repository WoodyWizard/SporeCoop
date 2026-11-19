#!/usr/bin/env python3
"""BUG-1282 (found by the Lineages fork (PR #196), its BUG-9131): meteors, rain storms and the night predator ran in each
game by itself: a guest had meteors and rain the host never had (and the host's never came to the guest), and its own
night predator, a creature of its game only. Now the host's start of a meteor strike or a rain storm starts the same
event in the guests' games, and a guest's own game starts none of the three in Creature.

The state checked is the game's own: each Drama Manager event's active byte (event +0xC, `coop.drama`: active=), on
the machine that should (or should not) show it. `coop.drama start <kind>` is the game's own StartKind of every idle
event of the kind (as the stage start does), the same path the manager's tick takes once an event's wait is over.
  1. guest: its own start of the night predator (1), the meteor strike (3), the rain storm (5): every one stays inactive
  2. host: its meteor strike starts (control) and the guest's meteor event is active within 4 s; the same for the rain

  SPORE_FIX_OFF=creature.shared_events coop_drama_shared.py   (1 and 2: must FAIL)
"""
import re
import time

from twoinst import Checks, keyframe, start

KINDS = {1: 'night_predator', 3: 'meteor', 5: 'rain'}


def event(con, kind):
    out = con.call('coop.drama')['out']
    m = re.search(r'^\s*' + KINDS[kind] + r' +started=(\d+) refused=(\d+) event=\S+ active=(-?\d+)', out, re.M)
    return {'started': int(m.group(1)), 'refused': int(m.group(2)), 'active': int(m.group(3))} if m else None


def active_within(con, kind, secs):
    t0 = time.time()
    e = None
    while time.time() - t0 < secs:
        e = event(con, kind)
        if e and e['active'] == 1:
            return e
        time.sleep(0.5)
    return e


def main():
    host, guest = start(fixture='creature')
    ck = Checks()
    time.sleep(5)
    print('  guest: ' + guest.call('coop.drama')['out'].strip().replace('\n', ' | '), flush=True)
    ck('host_sends=1' in guest.call('coop.drama')['out'], 'guest: the host relays its drama events (its build is 0.7.16)')

    for k in (1, 3, 5):
        guest.call(f'coop.drama start {k}')
        time.sleep(2)
        e = event(guest, k)
        print(f'  1. guest started its own {KINDS[k]}: {e}', flush=True)
        ck(e is not None and e['active'] != 1, f'1. guest: its own {KINDS[k]} event stays inactive ({e})')
    keyframe('guest_own_events', "Creature: the guest asked its own game for a night predator, meteors and rain: the "
             "guest's sky is as the host's (no rain, no meteors, no predator of its own)")

    for k, what in ((3, 'meteors fall from the sky'), (5, 'rain falls, the sky is dark')):
        host.call(f'coop.drama start {k}')
        he = active_within(host, k, 4)
        ge = active_within(guest, k, 4)
        print(f'  2. host started its {KINDS[k]}: host {he}, guest {ge}', flush=True)
        ck(he is not None and he['active'] == 1, f'2. host (control): its {KINDS[k]} event runs ({he})')
        ck(ge is not None and ge['active'] == 1, f'2. guest: the host\'s {KINDS[k]} runs in the guest\'s game too ({ge})')
        keyframe(f'host_{KINDS[k]}', f"Creature: the host's {KINDS[k]} event: {what} in BOTH games (host and guest)",
                 settle=0)
        time.sleep(2)
    print('  guest: ' + guest.call('coop.drama')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
