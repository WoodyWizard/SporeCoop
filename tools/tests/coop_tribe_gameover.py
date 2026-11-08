#!/usr/bin/env python3
"""Tribe: a member added while the game-over caption is up (BUG-0172).

When no member of the player tribe has health left the game shows "Alas! Your population has
fallen below the minimum needed to survive ... Bounce back in time" and waits for OK (the bounce
back reloads the last save). A member added to the player tribe meanwhile has no species fields;
the OK then faulted at 0xC0DAE3 and the host froze for good (2 of 2 tries, 2026-10-23).

1. The host knocks out every member of the player tribe through the game's damage path
   (`coop.tribe pincer <member> <enemy> 300`), the caption comes.
2. `coop.tribe spawn 2` on the host (what a guest's mirror spawn does on its machine): with the fix
   no member is added to a tribe that is out.
3. OK (`key enter`): the host bounces back and keeps drawing, no fault in either game.

--nofix: the old way (`coop.tribe fix gameover 0`): must FAIL.
--after-ok S (BUG-0172, 0.7.11 N1): the spawn comes S seconds after the OK instead, while the bounce back reloads
the save (the member's creation then meets a tribe being torn down); --count N members.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_gameover.py [--nofix]
"""
import argparse
import re
import time

import twoinst
from twoinst import Checks, start


def player_members(con):
    """{id: hp} of the player tribe, [enemy ids]"""
    own, enemy, player = {}, [], False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=\d+ hp=([\d.]+)', line)
        if m and m.group(1) != '0':
            if player:
                own[int(m.group(1))] = float(m.group(3))
            else:
                enemy.append(int(m.group(1)))
    return own, enemy


def frames_go_on(con, secs):
    f0 = twoinst.host_frame(con)
    time.sleep(secs)
    return twoinst.host_frame(con) > f0 + 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--after-ok', type=float, default=None)
    ap.add_argument('--count', type=int, default=2)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.tribe fix gameover 0')['out'].strip())
    time.sleep(6)
    own, enemy = player_members(host)
    print('  player tribe', own, 'enemies', enemy[:4])
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
    print('  player tribe now', own)
    ck(own and not [i for i, hp in own.items() if hp > 0], 'every member of the player tribe is out')
    time.sleep(15)  # the caption comes some seconds later (play_tribe.py waits 15 s too)
    twoinst.shot(host, 'tribe_gameover_caption.png')
    for word in ('Alas', 'Bounce', 'population'):
        print(f'  ui "{word}":', host.call(f'coop.ui find "{word}"')['out'].strip()[:300])
    if a.after_ok is None:
        print(' ', host.call(f'coop.tribe spawn {a.count}')['out'].strip())
        time.sleep(1)
    host.call('key enter')  # OK: bounce back in time
    if a.after_ok is not None:
        time.sleep(a.after_ok)
        for _ in range(3):
            print(f'  {a.after_ok:.1f} s after OK:', host.call(f'coop.tribe spawn {a.count}')['out'].strip()[:160])
            time.sleep(0.3)
    ok = frames_go_on(host, 15)
    ck(ok, 'the host bounces back and keeps drawing')
    t1 = time.time()
    while time.time() - t1 < 40:  # the bounce back reloads the last save: members with health again
        own, _ = player_members(host)
        if [i for i, hp in own.items() if hp > 0]:
            break
        time.sleep(2)
    print('  player tribe after OK', own)
    ck([i for i, hp in own.items() if hp > 0], 'after the bounce back the player tribe has members again')
    print(' ', [l for l in host.call('coop.status')['out'].splitlines() if 'tribe lifetime' in l])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
