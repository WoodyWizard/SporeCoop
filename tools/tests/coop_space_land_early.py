#!/usr/bin/env python3
"""Space: the mouse wheel lands on the home planet right after a world load (BUG-0182).

On GOG 2024 lanes, right after `twoinst.start('space')` a click on the home planet flew the ship
there but zooming in did not land for 50-80 s. Both players here click the home planet and zoom in
at once, the way a player does after loading; `coop.space landcheck` prints the game's own landing
conditions (the ship at its destination, HP, the wheel's readiness) while they try. A landing
pauses the game time (`paused 1`, cSimulatorSpaceGame +0x50 bit 2) for ~1 s while the planet
loads; the time counted is the first sample in the planet context.

  SPORE_LANE=9 python3 tools/tests/coop_space_land_early.py [--limit S]

--limit: seconds after start() within which each player must be on the planet (default 40).
"""
import sys
import threading
import time

import fixtures
import lane
from play_space import Player
from twoinst import Checks, shot, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def try_land(p, con, home, t0, limit, out):
    """click + zoom until on the planet; returns the seconds after start() it took (or None)"""
    while time.time() - t0 < limit + 60 and p.name not in out:
        r = sp(con, 'landcheck')
        print(f'  {p.name} {time.time() - t0:5.1f} s: {r}', flush=True)
        if r.startswith('ctx 0'):
            break
        if ' paused 1' in r:  # the landing itself pauses the game time while the planet loads
            time.sleep(0.5)
            continue
        p.land(home)


def sample(con, who, t0, stop, out):
    """the landing conditions every second; a screenshot the first time the game time is paused"""
    shot_taken = False
    while not stop.is_set():
        r = sp(con, 'landcheck')
        print(f'  {who} sample {time.time() - t0:5.1f} s: {r}', flush=True)
        if r.startswith('ctx 0') and who not in out:
            out[who] = time.time() - t0  # on the planet (context 0): the first sample that sees it
        if ' paused 1' in r and not shot_taken:
            shot_taken = True
            print(f'  {who} paused: {shot(con, f"land_early_{who}_paused.png")}', flush=True)
        stop.wait(1.0)


def main():
    limit = float(sys.argv[sys.argv.index('--limit') + 1]) if '--limit' in sys.argv else 40.0
    host, guest = start('space')
    t0 = time.time()
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 600, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 600, stats)
    home = fixtures.FIXTURES['space']['home']
    took = {}
    th = [threading.Thread(target=try_land, args=(p, c, home, t0, limit, took)) for p, c in ((ph, host), (pg, guest))]
    stop = threading.Event()
    th += [threading.Thread(target=sample, args=(c, w, t0, stop, took), daemon=True) for w, c in (('host', host), ('guest', guest))]
    for t in th:
        t.start()
    for t in th[:2]:
        t.join()
    time.sleep(1.5)
    stop.set()
    for who in ('host', 'guest'):
        s = took.get(who)
        ck(s is not None and s <= limit,
           f'{who}: the wheel landed on the home planet {f"{s:.0f} s" if s is not None else "never"} after the load '
           f'(at most {limit:.0f} s)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
