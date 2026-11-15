#!/usr/bin/env python3
"""BUG-0828: a spice gift that comes back is matched to the newest gift of that amount, not an older one.

Every co-op spice gift (the co-op window's Give, `coop.space give`) stays in the sender's list: only a failure is
answered. The answer was matched to the OLDEST entry of the same partner, amount and spice.

1. The host gives the guest 10 spice: it arrives (no answer, the entry stays).
2. The host goes to the galaxy menu and loads another game (game B: a copy of the fixture's slot made at the start):
   another Space world.
3. The guest has no room (`gift noroom 1`); the host gives 10 spice again: it comes back. It must be back in the
   host's cargo hold of game B at once.
Before the fix the answer took the entry of step 1 (game A's world): the refund was kept for game A (or dropped), and
game B's hold was 10 spice short. SPORE_FIX_OFF=space.gift_match_newest: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_space_gift_reload.py
"""
import os
import re
import shutil
import time

import fixtures
import lane
from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def spice(con):
    m = re.search(r'spice slot -?\d+ count (\d+)', sp(con, 'spice'))
    return int(m.group(1)) if m else None


def wait(pred, timeout=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def load(host, ck, slot, planet, what):
    print('  menu:', host.call('coop.menu')['out'].strip())
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90, 1), f'the host is in the galaxy menu ({what})')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load {slot} {planet}')['out'].strip())
    ck(wait(lambda: 'Space' in host.call('game.mode')['out'], 180, 2), f'the host is in Space: {what}')
    time.sleep(6)
    print('  world:', sp(host, 'world'))


def main():
    planet = fixtures.planet_of('space')
    slot_a, name = planet.split('/')
    games = os.path.join(lane.HOST_DIR, 'Games')
    made = []
    try:
        return run(slot_a, name, games, made)
    finally:
        for d in made:
            shutil.rmtree(d, ignore_errors=True)


def run(slot_a, name, games, made):
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    if (spice(host) or 0) < 25:
        print('  host spice:', sp(host, 'spice make 30'))
    slot_b = next(f'Game{n}' for n in range(7, 30) if not os.path.exists(os.path.join(games, f'Game{n}')))
    shutil.copytree(os.path.join(games, slot_a), os.path.join(games, slot_b))
    made.append(os.path.join(games, slot_b))
    print(f'  game B: {slot_b}/{name} (a copy of {slot_a})', flush=True)

    c0, g0 = spice(host), spice(guest) or 0
    print(f'  host spice {c0}, guest spice {g0}; give:', sp(host, 'give 10'), flush=True)
    ck(wait(lambda: spice(host) == c0 - 10, 5), f'10 spice left the host\'s hold ({spice(host)})')
    ck(wait(lambda: (spice(guest) or 0) >= g0 + 10, 15), f'the guest got them ({g0} -> {spice(guest)})')
    print('  host:', sp(host, 'gift'), flush=True)

    load(host, ck, slot_b, name, 'game B')
    ck(wait(lambda: 'Space' in guest.call('game.mode')['out'] and spice(guest) is not None, 180, 2),
       'the guest plays Space')
    time.sleep(5)
    print('  guest:', sp(guest, 'gift noroom 1'), flush=True)
    c1 = spice(host)
    if (c1 or 0) < 15:
        print('  host spice:', sp(host, 'spice make 30'))
        c1 = spice(host)
    print(f'  host spice in game B: {c1}; give:', sp(host, 'give 10'), flush=True)
    ck(wait(lambda: spice(host) == c1 - 10, 5), f'10 spice left the host\'s hold ({spice(host)})')
    ok = wait(lambda: spice(host) == c1, 15)
    print('  host:', sp(host, 'gift'), '| spice', spice(host), flush=True)
    ck(ok, f'the gift came back into game B\'s hold: {c1} ({spice(host)})')
    time.sleep(8)
    ck(spice(host) == c1, f'and stays, once ({spice(host)})')

    sp(guest, 'gift noroom 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
