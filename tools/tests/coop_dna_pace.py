#!/usr/bin/env python3
"""The party evolves at the single-player pace (BUG-0510: players got 3-4x the DNA of a solo game).

Two players, each earns the same known amount through the game's own reward function (Creature:
AddEvolutionPoints, `coop.dna gain 20` = a kill's / a friend's reward, hooked like the game's call;
Cell: AddFoodPoints as AvatarEat makes it, `coop.pool meal 20`). Checked on BOTH games, the game's own
value (GetEvolutionPoints; the Cell food total S+0x1C that the cell editor's DNA comes from):
  1. with the pace (default): the shared total grows by ONE player's amount (20), not the sum;
  2. a spend (-10, the guest's own) is not scaled: both lose 10;
  3. coop.ini dna_pace=0 on the host (`coop.dna pace 0`, the player's choice): the full sum (40).
SPORE_FIX_OFF=core.dna_pace must FAIL step 1 (the sum, 40).

  coop_dna_pace.py [--cell]
"""
import re
import sys
import time

from twoinst import Checks, start

CELL = '--cell' in sys.argv
GAIN = 20


def value(con):
    if CELL:
        m = re.search(r'^cell\((?:host|guest)\): food=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
        return int(m.group(1)) if m else None
    m = re.search(r'dna=(-?[\d.]+)', con.call('coop.dna')['out'])
    return float(m.group(1)) if m else None


def pace(con):
    m = re.search(r'dna pace: (.*)', con.call('coop.status')['out'])
    return m.group(1) if m else '?'


def gain(con, n):
    return con.call(f'coop.pool meal {n}' if CELL else f'coop.dna gain {n}')['out'].strip()


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def settle(host, guest, secs=12):
    """both games show the same total for 3 s in a row (the host's total reached the guest)"""
    t0, same_since, last = time.time(), None, None
    while time.time() - t0 < secs:
        h, g = value(host), value(guest)
        if h is not None and g is not None and abs(h - g) < 1 and h == last:
            same_since = same_since or time.time()
            if time.time() - same_since >= 3:
                return h, g
        else:
            same_since = None
        last = h
        time.sleep(0.5)
    return value(host), value(guest)


def both_earn(host, guest, what):
    h0, g0 = settle(host, guest)
    print(f'  {what}: before host {h0} guest {g0}; host: {gain(host, GAIN)}', flush=True)
    time.sleep(1.5)
    print(f'  {what}: guest: {gain(guest, GAIN)}', flush=True)
    time.sleep(1.5)
    h1, g1 = settle(host, guest, 20)
    print(f'  {what}: after host {h1} guest {g1}; pace host [{pace(host)}] guest [{pace(guest)}]', flush=True)
    return h0, h1, g0, g1


def main():
    host, guest = start('cell' if CELL else 'creature')
    ck = Checks()
    try:
        if CELL:
            wait(lambda: value(host) is not None and value(guest) is not None, 30)
        time.sleep(4)  # the guest's state reached the host (it counts the players in its world)
        # 1. the party's pace: two gains of 20 make the shared total grow by 20
        h0, h1, g0, g1 = both_earn(host, guest, 'pace')
        ok_h = h0 is not None and h1 is not None and abs((h1 - h0) - GAIN) < 1.01
        ok_g = g0 is not None and g1 is not None and abs((g1 - g0) - GAIN) < 1.01
        ck(ok_h, f"the host's game: the shared total grew by one player's {GAIN} (got {None if h1 is None or h0 is None else h1 - h0})")
        ck(ok_g, f"the guest's game: the shared total grew by one player's {GAIN} (got {None if g1 is None or g0 is None else g1 - g0})")
        # 2. a spend is whole (Creature: the guest's own spend, BUG-0466's path)
        if not CELL:
            s0, _ = settle(host, guest)
            guest.call('coop.dna add -10')
            s1, s2 = settle(host, guest, 20)
            print(f'  spend: guest -10: host {s0} -> {s1}, guest -> {s2}', flush=True)
            ck(s0 is not None and s1 is not None and abs((s0 - s1) - 10) < 1.01 and abs((s2 or 0) - s1) < 1,
               'a spend is not scaled: both games lost 10')
        # 3. the player's choice: dna_pace=0 on the host is the full sum
        print('  host:', host.call('coop.dna pace 0')['out'].strip(), '(coop.ini [coop] dna_pace=0)', flush=True)
        h0, h1, g0, g1 = both_earn(host, guest, 'full sum')
        ck(h0 is not None and h1 is not None and abs((h1 - h0) - 2 * GAIN) < 1.01,
           f'dna_pace=0: the host\'s total grew by the sum {2 * GAIN} (got {None if h1 is None or h0 is None else h1 - h0})')
        ck(g0 is not None and g1 is not None and abs((g1 - g0) - 2 * GAIN) < 1.01,
           f'dna_pace=0: the guest\'s total grew by the sum {2 * GAIN} (got {None if g1 is None or g0 is None else g1 - g0})')
        host.call('coop.dna pace 1')
        ck.faults(host=host, guest=guest)
    finally:
        pass
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
