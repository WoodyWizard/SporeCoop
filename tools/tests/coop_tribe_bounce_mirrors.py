#!/usr/bin/env python3
"""Tribe: after the host's game over and bounce back the guests mirror the host's tribe again (BUG-0288).

The final 4-player campaign lost the host's whole player tribe in a raid: the game-over caption
came, the host bounced back (the game reloads the last save) and from then on every guest missed
16 of the host's members (sync-check "t: 16 missing on the guest"; the campaign's coverage
desync). The guest's player tribe holds mirrors only: when every mirror had played its death (no
health left) the guest's BUG-0172 guard ("no member is added to a tribe that is out") refused every
new mirror for good, also after the dead ones had been removed (an empty tribe counts as out).

1. Tribe fixture, 3 more members; the host knocks out its whole player tribe through the game's
   damage path (`coop.tribe pincer <member> <enemy> 300`); the guest's mirrors die too.
2. The game-over caption on the host; what the guest shows meanwhile is printed (caption, lock).
3. OK on the host: it bounces back. Within 40 s the GUEST's game has as many members in its
   player tribe as the host's (the game's own tribe list, `coop.tribe`), and the guest shows every
   member of the host (coop.sync t keys).

--nofix: `coop.fix tribe.mirror_out off` (the guard as in 0.7.3.1): must FAIL.

  SPORE_LANE=8 python3 tools/tests/coop_tribe_bounce_mirrors.py [--nofix]
"""
import argparse
import re
import time

import twoinst
from coop_multi import sample
from coop_tribe_gameover import frames_go_on, player_members
from twoinst import Checks, start


def player_tribe_size(con):
    m = re.search(r'^tribe \d+ \(player\) at .*?: (\d+) members', con.call('coop.tribe')['out'], re.M)
    return int(m.group(1)) if m else -1


def lock(con):
    m = re.search(r'input_lock=(\d+)', con.call('coop.uistate')['out'])
    return m.group(1) if m else '?'


def t_missing(host, guest):
    h, g = sample(host), sample(guest)
    hk = [k for k in h if k.startswith('t')]
    return [k for k in hk if k not in g], len(hk)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix tribe.mirror_out off')['out'].strip())
    time.sleep(6)
    print(' ', host.call('coop.tribe spawn 3')['out'].strip())
    time.sleep(6)
    miss, n = t_missing(host, guest)
    print(f'  before: host player tribe {player_tribe_size(host)}, guest {player_tribe_size(guest)}, '
          f'missing on the guest {miss} of {n}')
    ck(n > 0 and not miss, 'before the game over the guest shows every member of the host')
    own, enemy = player_members(host)
    ck(own and enemy, 'the player tribe and another tribe have members')
    if not (own and enemy):
        return ck.result()
    t0 = time.time()
    while time.time() - t0 < 60:
        own, enemy = player_members(host)
        alive = [i for i, hp in own.items() if hp > 0]
        if not alive or not enemy:
            break
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        time.sleep(1)
    ck(own and not [i for i, hp in own.items() if hp > 0], 'every member of the player tribe is out')
    t2, alas = time.time(), False
    while time.time() - t2 < 40 and not alas:  # the caption comes some seconds later
        time.sleep(3)
        alas = 'no window' not in host.call('coop.ui find "Alas"')['out']
    galas = 'no window' not in guest.call('coop.ui find "Alas"')['out']
    print(f'  caption: host {alas} (lock {lock(host)}), guest {galas} (lock {lock(guest)}), guest player '
          f'tribe {player_tribe_size(guest)} members')
    twoinst.shot(guest, 'tribe_bounce_mirrors_guest_caption.png')
    ck(alas, 'the game-over caption is up on the host')
    host.call('key enter')  # OK: bounce back in time
    ck(frames_go_on(host, 15), 'the host bounces back and keeps drawing')
    t1, hs, gs, miss, n = time.time(), -1, -1, [], 0
    while time.time() - t1 < 40:
        hs, gs = player_tribe_size(host), player_tribe_size(guest)
        miss, n = t_missing(host, guest)
        if hs > 0 and gs == hs and not miss:
            break
        time.sleep(3)
    print(f'  after the bounce back: host player tribe {hs}, guest {gs}, missing on the guest {miss} of {n}')
    print(' ', [l for l in guest.call('coop.tribe')['out'].splitlines() if 'tribe lifetime' in l])
    twoinst.shot(guest, 'tribe_bounce_mirrors_guest_after.png')
    ck(hs > 0, 'the host has its tribe back')
    ck(gs == hs, f'the guest\'s game has the host\'s player tribe ({gs} of {hs} members)')
    ck(n > 0 and not miss, f'the guest shows every member of the host ({len(miss)} of {n} missing)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
